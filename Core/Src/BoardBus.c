#if defined(FW_CUSTOM_HMI)&&defined(FW_FINAL_APPLICATION)
#include "BoardBus.h"
#include "RtuMaster.h"
#include "HmiMonitorIo.h"
#include "BurnerApp.h"
#include "DiagLog.h"
#include "usart.h"
#include <string.h>
static RtuMaster master;
static BoardBusSnapshot snapshot;
static DMA_HandleTypeDef tx_dma;
static GPIO_TypeDef *de_port;
static uint16_t de_pin;
static const uint8_t *tx_data;
static uint16_t tx_size;
static volatile bool tx_active,tx_setup;
static uint32_t tx_cycles,tx_ms,poll_ms;
static uint16_t last_command_sequence;
static bool command_synced;
static uint8_t slot;
static uint32_t command_errors;
static uint16_t Word(const uint8_t *p) {return (uint16_t)(((uint16_t)p[0]<<8)|p[1]);}
static bool Start(void *unused,const uint8_t *data,uint16_t size)
{
    (void)unused;
    if(!snapshot.configured||tx_active||huart1.gState!=HAL_UART_STATE_READY)return false;
    de_port->BSRR=de_pin;tx_data=data;tx_size=size;tx_cycles=DWT->CYCCNT;tx_ms=HAL_GetTick();
    tx_setup=tx_active=true;++snapshot.tx_count;return true;
}
static bool Transmitting(void *unused) {(void)unused;return tx_active;}
void BoardBus_TxCompleteIRQ(void)
{if(de_port)de_port->BSRR=(uint32_t)de_pin<<16;tx_active=tx_setup=false;}
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart) {if(uart==&huart1)BoardBus_TxCompleteIRQ();}
void BoardBus_TxDmaIRQ(void) {HAL_DMA_IRQHandler(&tx_dma);}
void BoardBus_Init(void)
{
    const BoardBusConfig *c=&board_bus_config;
    Rtu_Init(&master,(RtuIo){NULL,Start,Transmitting},c->quiet_ms,c->timeout_ms,c->retries);
    if(!c->enabled||!c->direction_confirmed||c->de_pin>15U)return;
    switch(c->de_port){case 'A':de_port=GPIOA;break;case 'B':de_port=GPIOB;break;case 'C':de_port=GPIOC;break;case 'D':de_port=GPIOD;break;default:return;}
    de_pin=(uint16_t)(1U<<c->de_pin);HAL_GPIO_WritePin(de_port,de_pin,GPIO_PIN_RESET);
    GPIO_InitTypeDef gpio={0};gpio.Pin=de_pin;gpio.Mode=GPIO_MODE_OUTPUT_PP;gpio.Pull=GPIO_PULLDOWN;gpio.Speed=GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(de_port,&gpio);
    tx_dma.Instance=DMA2_Stream7;tx_dma.Init.Channel=DMA_CHANNEL_4;tx_dma.Init.Direction=DMA_MEMORY_TO_PERIPH;
    tx_dma.Init.PeriphInc=DMA_PINC_DISABLE;tx_dma.Init.MemInc=DMA_MINC_ENABLE;
    tx_dma.Init.PeriphDataAlignment=DMA_PDATAALIGN_BYTE;tx_dma.Init.MemDataAlignment=DMA_MDATAALIGN_BYTE;
    tx_dma.Init.Mode=DMA_NORMAL;tx_dma.Init.Priority=DMA_PRIORITY_HIGH;tx_dma.Init.FIFOMode=DMA_FIFOMODE_DISABLE;
    if(HAL_DMA_Init(&tx_dma)!=HAL_OK)return;
    __HAL_LINKDMA(&huart1,hdmatx,tx_dma);HAL_NVIC_SetPriority(DMA2_Stream7_IRQn,2,0);HAL_NVIC_EnableIRQ(DMA2_Stream7_IRQn);
    snapshot.configured=true;
}
static void Result(uint32_t now)
{
    if(slot==0U) {
        uint16_t sequence=Word(master.rx+3),command=Word(master.rx+5),mode=Word(master.rx+7),target=Word(master.rx+9);
        snapshot.plc_ms=now;snapshot.plc_valid=true;
        if(command<=3U&&(command&2U))BurnerApp_Stop(BURNER_PLC);
        if(command>3U||mode>1U||target<100U||target>5000U||target%10U) {++command_errors;snapshot.plc_valid=false;return;}
        bool fresh=command_synced&&sequence!=last_command_sequence;
        if(!command_synced||fresh) {
            if(fresh) {
                (void)BurnerApp_SetMode(mode?BURNER_AUTO_MODE:BURNER_MANUAL_MODE);(void)BurnerApp_SetCO2Target(target);
                if((command&1U)&&!(command&2U))(void)BurnerApp_Start(BURNER_PLC);
            }
            last_command_sequence=sequence;command_synced=true;
        }
    } else if(slot==1U) {
        snapshot.lux_centi=((uint32_t)Word(master.rx+3)<<16)|Word(master.rx+5);
        snapshot.lux_ms=now;snapshot.lux_valid=true;
        if(snapshot.lux_centi>=board_bus_config.lux_day_on_centi)snapshot.daylight=true;
        else if(snapshot.lux_centi<=board_bus_config.lux_day_off_centi)snapshot.daylight=false;
    }
}
void BoardBus_Process(void)
{
    uint32_t now=HAL_GetTick();const BoardBusConfig *c=&board_bus_config;
    if(tx_active&&now-tx_ms>=c->timeout_ms) {
        BoardBus_TxCompleteIRQ();(void)HAL_UART_AbortTransmit_IT(&huart1);
    }
    if(tx_setup&&(uint32_t)(DWT->CYCCNT-tx_cycles)>=SystemCoreClock/50000U) {
        tx_setup=false;
        uint32_t mask=__get_PRIMASK();__disable_irq();
        HAL_StatusTypeDef result=HAL_UART_Transmit_DMA(&huart1,(uint8_t *)(uintptr_t)tx_data,tx_size);
        if(result==HAL_OK)__HAL_DMA_DISABLE_IT(&tx_dma,DMA_IT_HT);
        __set_PRIMASK(mask);
        if(result!=HAL_OK)BoardBus_TxCompleteIRQ();
    }
    /* Transition TX->reply before consuming a fast slave's buffered response. */
    Rtu_Process(&master,now);
    uint8_t data[64];size_t n=HmiMonitorIo_ReadRx(data,sizeof(data));
    if(n)Rtu_Receive(&master,data,n,now);
    bool success;
    if(Rtu_TakeResult(&master,&success)) {
        if(success)Result(now);
        else DiagLog_Write(now,DIAG_WARN,"RS485","slot=%u timeout=%lu CRC=%lu exception=%u",slot,(unsigned long)master.timeouts,(unsigned long)master.crc_errors,master.exception);
        slot=(uint8_t)((slot+1U)%(c->debug_enabled?4U:3U));poll_ms=now;
    }
    snapshot.active_slot=slot;snapshot.completed=master.completed;snapshot.timeouts=master.timeouts;
    snapshot.crc_errors=master.crc_errors;snapshot.retries=master.retry_count;snapshot.last_exception=master.exception;
    snapshot.protocol_errors=master.protocol_errors+command_errors;
    if(snapshot.lux_valid&&now-snapshot.lux_ms>=c->lux_stale_ms)snapshot.lux_valid=false;
    if(snapshot.plc_valid&&now-snapshot.plc_ms>=c->lux_stale_ms)snapshot.plc_valid=false;
    if(!snapshot.configured||Rtu_Busy(&master)||now-poll_ms<c->poll_ms)return;
    if(slot==0U)(void)Rtu_Read(&master,c->plc_id,3,c->plc_command_register,4,now);
    else if(slot==1U)(void)Rtu_Read(&master,c->lux_id,4,c->lux_register,2,now);
    else if(slot==2U) {
        BurnerAppSnapshot b;BurnerApp_GetSnapshot(&b);
        uint16_t values[]={(uint16_t)b.state,(uint16_t)b.fault,(uint16_t)b.sample_state,(uint16_t)b.command_source,
            (uint16_t)(b.fault_sequence>>16),(uint16_t)b.fault_sequence,b.actuators_inhibited,b.co2_target_ppm,(uint16_t)b.fault_source};
        (void)Rtu_WriteRegisters(&master,c->plc_id,c->plc_status_register,values,9,now);
    } else {
        DiagEntry entry;uint16_t values[48]={0};
        if(DiagLog_Count()&&DiagLog_Read(DiagLog_Count()-1U,&entry)) {
            values[0]=(uint16_t)(entry.sequence>>16);values[1]=(uint16_t)entry.sequence;
            values[2]=(uint16_t)(entry.ms>>16);values[3]=(uint16_t)entry.ms;values[4]=(uint16_t)entry.level;
            for(unsigned i=0;i<DIAG_LOG_MESSAGE&&i<86&&entry.text[i];++i)values[5+i/2]|=(uint16_t)((uint16_t)(uint8_t)entry.text[i]<<(i%2?0:8));
        }
        (void)Rtu_WriteRegisters(&master,c->debug_id,0,values,48,now);
    }
}
void BoardBus_Get(BoardBusSnapshot *out)
{if(out)*out=snapshot;}
#endif
