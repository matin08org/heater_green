#include "HC595_SensorBus.h"
#include <stddef.h>

#define TRANSFER_TIMEOUT_MS 10U
typedef enum { IDLE, OUTPUT_TX, SENSOR_TX, SENSOR_SETTLE, SENSOR_RX, SENSOR_DONE, RECOVERING } BusState;
static BusState state;
static BusState recovery_target;
static uint8_t initialized, shadow, dirty, frame, want_read, previous_q0;
static uint8_t tx[2], rx[2];
static volatile uint8_t event_done, event_error;
static uint32_t transfer_ms, settle_cycles, latch_cycles;
static HC595_SensorResult_t result;
static uint32_t errors,latches,last_latch_ms;
static volatile uint32_t last_error;
static uint8_t latched;
static uint32_t Lock(void) { uint32_t p=__get_PRIMASK(); __disable_irq(); return p; }
static void Unlock(uint32_t p) { __set_PRIMASK(p); }

/* Exclusive bus owner. IRQs only publish flags; Process owns state/latching.
 * Public output setters can run in IRQ/task context under a short critical
 * section; Process, Begin and Poll are serviced from one main-context owner. */
static HAL_StatusTypeDef BeginTransfer(BusState next, uint16_t size)
{
    HAL_StatusTypeDef s;
    uint32_t p=Lock();
    event_done=event_error=0U;
    state=next;
    transfer_ms=HAL_GetTick();
    /* RX DMA drains DR independently of CPU/IRQ latency while executing XIP. */
    s=HAL_SPI_TransmitReceive_DMA(&hspi2,tx,rx,size);
    Unlock(p);
    return s;
}

static void RecoverTransfer(BusState destination)
{
    errors++;
    last_error=hspi2.ErrorCode ? hspi2.ErrorCode : 0x80000000U;
    /* Cancel requests first, then stop both streams. Stream EN may clear later;
     * Process retains bus ownership until that happens, without a busy wait. */
    uint32_t p=Lock();
    __HAL_SPI_DISABLE_IT(&hspi2,SPI_IT_TXE|SPI_IT_RXNE|SPI_IT_ERR);
    CLEAR_BIT(hspi2.Instance->CR2,SPI_CR2_RXDMAEN|SPI_CR2_TXDMAEN);
    __HAL_DMA_DISABLE(hspi2.hdmarx);
    __HAL_DMA_DISABLE(hspi2.hdmatx);
    __HAL_SPI_DISABLE(&hspi2);
    recovery_target=destination;
    state=RECOVERING;
    Unlock(p);
}

static void FinishRecovery(void)
{
    DMA_HandleTypeDef *streams[2]={hspi2.hdmarx,hspi2.hdmatx};
    uint32_t p=Lock();
    if ((streams[0]->Instance->CR|streams[1]->Instance->CR)&DMA_SxCR_EN) {
        Unlock(p);
        return;
    }
    for (uint32_t i=0;i<2U;i++) {
        DMA_HandleTypeDef *dma=streams[i];
        __HAL_DMA_DISABLE_IT(dma,DMA_IT_TC|DMA_IT_HT|DMA_IT_TE|DMA_IT_DME|DMA_IT_FE);
        __HAL_DMA_CLEAR_FLAG(dma,__HAL_DMA_GET_TC_FLAG_INDEX(dma)|
            __HAL_DMA_GET_HT_FLAG_INDEX(dma)|__HAL_DMA_GET_TE_FLAG_INDEX(dma)|
            __HAL_DMA_GET_DME_FLAG_INDEX(dma)|__HAL_DMA_GET_FE_FLAG_INDEX(dma));
        dma->State=HAL_DMA_STATE_READY;
        dma->Lock=HAL_UNLOCKED;
        dma->ErrorCode=HAL_DMA_ERROR_NONE;
    }
    __HAL_SPI_CLEAR_OVRFLAG(&hspi2);
    hspi2.State=HAL_SPI_STATE_READY;
    hspi2.Lock=HAL_UNLOCKED;
    hspi2.ErrorCode=HAL_SPI_ERROR_NONE;
    event_done=event_error=0U;
    state=recovery_target;
    Unlock(p);
}

HAL_StatusTypeDef HC595_Init(void)
{
    if (initialized) return HAL_OK;
    if (!hspi2.hdmarx || !hspi2.hdmatx || hspi2.Instance!=SPI2 || hspi2.Init.Mode!=SPI_MODE_MASTER ||
        hspi2.Init.Direction!=SPI_DIRECTION_2LINES || hspi2.Init.DataSize!=SPI_DATASIZE_8BIT ||
        hspi2.Init.CLKPolarity!=SPI_POLARITY_LOW || hspi2.Init.CLKPhase!=SPI_PHASE_1EDGE ||
        hspi2.Init.FirstBit!=SPI_FIRSTBIT_MSB) return HAL_ERROR;
    HAL_GPIO_WritePin(HC595_STCP_GPIO_Port,HC595_STCP_Pin,GPIO_PIN_RESET);
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    shadow=1U; /* Startup conversion belongs to sensor 1. */
    dirty=1U;
    state=IDLE;
    initialized=1U;
    return HAL_OK;
}

void HC595_Process(void)
{
    HAL_StatusTypeDef s;
    uint32_t p;
    if (!initialized || state==SENSOR_DONE) return;
    if (state==RECOVERING) { FinishRecovery(); return; }
    if (state==IDLE) {
        p=Lock();
        if (!dirty) { Unlock(p); return; }
        frame=shadow;
        tx[0]=frame;
        s=BeginTransfer(OUTPUT_TX,1U);
        if (s!=HAL_OK) RecoverTransfer(IDLE); /* Retain dirty image for retry. */
        Unlock(p);
        return;
    }
    if (state==SENSOR_SETTLE) {
        if ((uint32_t)(DWT->CYCCNT-latch_cycles)<settle_cycles) return;
        tx[0]=tx[1]=0U;
        if (BeginTransfer(SENSOR_RX,2U)!=HAL_OK) {
            result.status=HAL_ERROR;
            RecoverTransfer(SENSOR_DONE);
        }
        return;
    }
    if (event_error || (!event_done && (uint32_t)(HAL_GetTick()-transfer_ms)>=TRANSFER_TIMEOUT_MS)) {
        BusState failed=state;
        if (failed!=OUTPUT_TX) result.status=HAL_ERROR;
        RecoverTransfer(failed==OUTPUT_TX ? IDLE : SENSOR_DONE);
        return;
    }
    if (!event_done) return;
    __DMB(); /* Acquire RX bytes published by the SPI completion IRQ. */
    event_done=0U;
    if (state==SENSOR_RX) {
        result.raw=((uint16_t)rx[0]<<8)|rx[1];
        result.status=HAL_OK;
        state=SENSOR_DONE; /* Still locked until MAX6675 consumes result. */
        return;
    }
    /* Every latch is preceded by a freshly transmitted, complete image.
     * Sensor RX clocks dirty the SERIAL register only, never this latch. */
    HAL_GPIO_WritePin(HC595_STCP_GPIO_Port,HC595_STCP_Pin,GPIO_PIN_SET);
    /* Record AFTER the rising latch edge, before any further bus operation. */
    result.latch_ms=HAL_GetTick();
    latched=frame; latches++; last_latch_ms=result.latch_ms;
    latch_cycles=DWT->CYCCNT;
    HAL_GPIO_WritePin(HC595_STCP_GPIO_Port,HC595_STCP_Pin,GPIO_PIN_RESET);
    p=Lock();
    dirty=(shadow!=frame);
    Unlock(p);
    if (state==OUTPUT_TX) { state=IDLE; return; }
    result.latched=1U;
    result.status=HAL_OK;
    state=want_read ? SENSOR_SETTLE : SENSOR_DONE;
}

HAL_StatusTypeDef HC595_SensorBegin(uint8_t q0,uint8_t read,uint32_t settle_us)
{
    HAL_StatusTypeDef s;
    uint32_t p;
    if (!initialized || q0>1U || settle_us>1000000U) return HAL_ERROR;
    p=Lock();
    if (state!=IDLE) { Unlock(p); return HAL_BUSY; }
    previous_q0=shadow&1U;
    shadow=(shadow&0xFEU)|q0;
    dirty=1U;
    frame=shadow;
    tx[0]=frame;
    want_read=read;
    settle_cycles=(uint32_t)(((uint64_t)SystemCoreClock*settle_us+999999U)/1000000U);
    result=(HC595_SensorResult_t){.status=HAL_BUSY};
    s=BeginTransfer(SENSOR_TX,1U);
    if (s!=HAL_OK) {
        /* Keep ownership/result so a failed CS transfer is never latched. */
        result.status=HAL_ERROR;
        RecoverTransfer(SENSOR_DONE);
    }
    Unlock(p);
    return HAL_OK; /* Accepted, including asynchronously reported failures. */
}

HAL_StatusTypeDef HC595_SensorPoll(HC595_SensorResult_t *out)
{
    if (!out) return HAL_ERROR;
    if (state!=SENSOR_DONE) return HAL_BUSY;
    *out=result;
    /* On a failed select, do not let normal output writes latch the requested
     * Q0 behind the sensor state machine. Restore Q0 to the last known output. */
    if (!result.latched) {
        uint32_t p=Lock();
        shadow=(shadow&0xFEU)|previous_q0;
        Unlock(p);
    }
    state=IDLE;
    return HAL_OK;
}

HAL_StatusTypeDef HC595_WritePin(HC595_Output_t output,HC595_State_t value)
{
    uint32_t p;
    if (!initialized || output==HC595_QA || (uint32_t)output>=8U ||
        (value!=HC595_OFF && value!=HC595_ON)) return HAL_ERROR;
    p=Lock();
    uint8_t before=shadow;
    if (value==HC595_ON) shadow|=(uint8_t)(1U<<output);
    else shadow&=(uint8_t)~(1U<<output);
    if(shadow!=before)dirty=1U;
    Unlock(p);
    return HAL_OK;
}
HAL_StatusTypeDef HC595_TogglePin(HC595_Output_t output)
{
    uint32_t p;
    if (!initialized || output==HC595_QA || (uint32_t)output>=8U) return HAL_ERROR;
    p=Lock(); shadow^=(uint8_t)(1U<<output); dirty=1U; Unlock(p); return HAL_OK;
}
HAL_StatusTypeDef HC595_WriteByte(uint8_t data)
{
    uint32_t p;
    if (!initialized) return HAL_ERROR;
    p=Lock();
    uint8_t next=(data&0xFEU)|(shadow&1U);
    if(next!=shadow){shadow=next;dirty=1U;}
    Unlock(p);return HAL_OK;
}
HAL_StatusTypeDef HC595_SetAll(void) { return HC595_WriteByte(0xFFU); }
HAL_StatusTypeDef HC595_ClearAll(void) { return HC595_WriteByte(0U); }
HAL_StatusTypeDef HC595_Update(void) { uint32_t p=Lock(); dirty=1U; Unlock(p); return initialized?HAL_OK:HAL_ERROR; }
uint8_t HC595_GetShadow(void) { uint32_t p=Lock(); uint8_t s=shadow; Unlock(p); return s; }
HC595_State_t HC595_GetPinState(HC595_Output_t output) { return ((uint32_t)output<8U && (HC595_GetShadow()&(1U<<output)))?HC595_ON:HC595_OFF; }
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *spi) { if (spi==&hspi2) { __DMB(); event_done=1U; } }
void HC595_SPIError(SPI_HandleTypeDef *spi) { if (spi==&hspi2) event_error=1U; }
void HC595_GetDiagnostics(HC595_Diagnostics *out)
{
    if (!out) return;
    uint32_t p=Lock();
    *out=(HC595_Diagnostics){.errors=errors,.latches=latches,.last_latch_ms=last_latch_ms,
        .last_error=last_error,.state=(uint8_t)state,.shadow=shadow,.latched=latched,.initialized=initialized,.dirty=dirty};
    Unlock(p);
}
