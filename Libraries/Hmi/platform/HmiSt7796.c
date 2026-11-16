#include "HmiSt7796.h"
#include <string.h>
enum { RESET_LOW, RESET_HIGH, PANEL_INIT, READY, WINDOW, FAILED };
typedef struct { uint8_t command, count, data[14]; uint16_t delay_ms; } InitStep;
/* Board's known controller settings/orientation, with asynchronous delays.
 * No RAMWR is issued until a complete tile is ready. */
static const InitStep init[] = {
    {0x01,0,{0},120}, {0x11,0,{0},120},
    {0xf0,1,{0xc3},0}, {0xf0,1,{0x96},0},
    {0x36,1,{0xe8},0}, {0x3a,1,{0x55},0}, {0xb4,1,{1},0},
    {0xb6,3,{0x80,2,0x3b},0},
    {0xe8,8,{0x40,0x8a,0,0,0x29,0x19,0xa5,0x33},0},
    {0xc1,1,{6},0}, {0xc2,1,{0xa7},0}, {0xc5,1,{0x18},120},
    {0xe0,14,{0xf0,9,0x0b,6,4,0x15,0x2f,0x54,0x42,0x3c,0x17,0x14,0x18,0x1b},0},
    {0xe1,14,{0xe0,9,0x0b,6,4,3,0x2b,0x43,0x42,0x3b,0x16,0x14,0x17,0x1b},120},
    {0xf0,1,{0x3c},0}, {0xf0,1,{0x69},120}, {0x29,0,{0},20}
};
static HmiSt7796 *owner;
static bool Due(uint32_t now, uint32_t deadline) { return (int32_t)(now - deadline) >= 0; }
static void Done(DMA_HandleTypeDef *dma)
{
    (void)dma;
    CLEAR_BIT(hspi1.Instance->CR2, SPI_CR2_TXDMAEN);
    owner->dma_done = 1U; /* No BSY loop or rendering in IRQ. */
}
static void Error(DMA_HandleTypeDef *dma)
{
    (void)dma;
    CLEAR_BIT(hspi1.Instance->CR2, SPI_CR2_TXDMAEN);
    owner->dma_error = 1U;
}
static HmiResult Fail(HmiSt7796 *lcd)
{
    CLEAR_BIT(hspi1.Instance->CR2, SPI_CR2_TXDMAEN);
    __HAL_DMA_DISABLE(hspi1.hdmatx);
    __HAL_SPI_DISABLE(&hspi1);
    lcd->phase = FAILED; ++lcd->errors;
    /* Do not claim idle/release pixels after an error. Explicit reinit only
     * once DMA EN is observed clear. No retry using a half-transmitted tile. */
    return HMI_IO_ERROR;
}
static HmiResult Send(HmiSt7796 *lcd, const void *data, uint16_t count, bool words, bool dc)
{
    DMA_HandleTypeDef *dma = hspi1.hdmatx;
    __HAL_SPI_DISABLE(&hspi1);
    uint32_t width = words ? SPI_DATASIZE_16BIT : SPI_DATASIZE_8BIT;
    MODIFY_REG(hspi1.Instance->CR1, SPI_CR1_DFF, width);
    hspi1.Init.DataSize = width;
    uint32_t alignment = words ? DMA_MDATAALIGN_HALFWORD : DMA_MDATAALIGN_BYTE;
    if (dma->Init.MemDataAlignment != alignment) {
        dma->Init.PeriphDataAlignment = words ? DMA_PDATAALIGN_HALFWORD : DMA_PDATAALIGN_BYTE;
        dma->Init.MemDataAlignment = alignment;
        if (HAL_DMA_Init(dma) != HAL_OK) return Fail(lcd);
    }
    HAL_GPIO_WritePin(DC_GPIO_Port, DC_Pin, dc ? GPIO_PIN_SET : GPIO_PIN_RESET);
    uint32_t mask = __get_PRIMASK(); __disable_irq();
    lcd->dma_done = lcd->dma_error = 0U;
    HAL_StatusTypeDef result = HAL_SPI_Transmit_DMA(&hspi1, (uint8_t *)(uintptr_t)data, count);
    if (result == HAL_OK) {
        dma->XferCpltCallback = Done; dma->XferErrorCallback = Error;
        dma->XferHalfCpltCallback = NULL;
        __HAL_DMA_DISABLE_IT(dma, DMA_IT_HT);
        __HAL_SPI_DISABLE_IT(&hspi1, SPI_IT_ERR);
        lcd->transferring = true; lcd->transfer_started = HAL_GetTick();
    }
    __set_PRIMASK(mask);
    return result == HAL_OK ? HMI_BUSY : Fail(lcd);
}
static HmiResult Poll(void *context)
{
    HmiSt7796 *lcd = context;
    if (!lcd || lcd != owner) return HMI_INVALID;
    if (lcd->phase == FAILED) return HMI_IO_ERROR;
    uint32_t now = HAL_GetTick();
    if (lcd->transferring) {
        if (lcd->dma_error || now - lcd->transfer_started >= 100U) return Fail(lcd);
        if (!lcd->dma_done || __HAL_SPI_GET_FLAG(&hspi1, SPI_FLAG_BSY) ||
            !__HAL_SPI_GET_FLAG(&hspi1, SPI_FLAG_TXE)) return HMI_BUSY;
        hspi1.TxXferCount = 0U; hspi1.State = HAL_SPI_STATE_READY;
        lcd->transferring = false; lcd->dma_done = 0U;
        if (lcd->phase == PANEL_INIT) {
            const InitStep *step = &init[lcd->init_step];
            if (lcd->part == 0U && step->count) {
                lcd->part = 1U;
                return Send(lcd, step->data, step->count, false, true);
            }
            lcd->deadline = now + step->delay_ms;
            ++lcd->init_step; lcd->part = 0U;
        } else if (lcd->phase == WINDOW) ++lcd->part;
    }
    if (lcd->phase == RESET_LOW) {
        if (!Due(now, lcd->deadline)) return HMI_BUSY;
        HAL_GPIO_WritePin(RESET_GPIO_Port, RESET_Pin, GPIO_PIN_SET);
        lcd->deadline = now + 120U; lcd->phase = RESET_HIGH;
        return HMI_BUSY;
    }
    if (lcd->phase == RESET_HIGH) {
        if (!Due(now, lcd->deadline)) return HMI_BUSY;
        lcd->phase = PANEL_INIT;
    }
    if (lcd->phase == PANEL_INIT) {
        if (!Due(now, lcd->deadline)) return HMI_BUSY;
        if (lcd->init_step == sizeof(init) / sizeof(init[0])) { lcd->phase = READY; return HMI_OK; }
        return Send(lcd, &init[lcd->init_step].command, 1U, false, false);
    }
    if (lcd->phase == WINDOW) {
        if (lcd->part == 0U || lcd->part == 2U || lcd->part == 4U) {
            lcd->command = lcd->part == 0U ? 0x2aU : lcd->part == 2U ? 0x2bU : 0x2cU;
            return Send(lcd, &lcd->command, 1U, false, false);
        }
        if (lcd->part == 1U || lcd->part == 3U) {
            HmiRect r = lcd->rectangle;
            uint16_t start = (uint16_t)(lcd->part == 1U ? r.x : r.y);
            uint16_t end = (uint16_t)(start + (lcd->part == 1U ? r.w : r.h) - 1U);
            lcd->parameters[0] = (uint8_t)(start >> 8); lcd->parameters[1] = (uint8_t)start;
            lcd->parameters[2] = (uint8_t)(end >> 8); lcd->parameters[3] = (uint8_t)end;
            return Send(lcd, lcd->parameters, 4U, false, true);
        }
        if (lcd->part == 5U)
            return Send(lcd, lcd->pixels, (uint16_t)((uint32_t)lcd->rectangle.w * lcd->rectangle.h), true, true);
        lcd->phase = READY;
        HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_SET);
    }
    return lcd->phase == READY ? HMI_OK : HMI_BUSY;
}
static HmiResult Start(void *context, HmiRect r, const uint16_t *pixels)
{
    HmiSt7796 *lcd = context;
    if (!lcd || lcd != owner || !pixels || ((uintptr_t)pixels & 1U) || !r.w || !r.h ||
        r.x < 0 || r.y < 0 || (int64_t)r.x + r.w > 480 || (int64_t)r.y + r.h > 320 ||
        (uint32_t)r.w * r.h > UINT16_MAX) return HMI_INVALID;
    if (lcd->phase == FAILED) return HMI_IO_ERROR;
    if (lcd->phase != READY) return HMI_BUSY;
    lcd->rectangle = r; lcd->pixels = pixels; lcd->part = 0; lcd->phase = WINDOW;
    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_RESET);
    /* Launch the first command immediately; otherwise every tile waits for an
     * unrelated SysTick before its first DMA. Never wait for wire completion. */
    lcd->command=0x2aU;
    HmiResult result=Send(lcd,&lcd->command,1U,false,false);
    return result==HMI_BUSY?HMI_OK:result;
}
HmiResult HmiSt7796_Init(HmiSt7796 *lcd)
{
    if (!lcd || !hspi1.hdmatx || hspi1.Instance != SPI1 || HAL_RCC_GetPCLK2Freq() != 64000000U)
        return HMI_INVALID;
    if ((hspi1.hdmatx->Instance->CR & DMA_SxCR_EN) ||
        __HAL_SPI_GET_FLAG(&hspi1, SPI_FLAG_BSY)) return HMI_BUSY;
    if (owner && owner != lcd) return HMI_BUSY;
    __HAL_SPI_DISABLE(&hspi1);
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
    MODIFY_REG(hspi1.Instance->CR1, SPI_CR1_DFF | SPI_CR1_BR,
               SPI_DATASIZE_8BIT | SPI_BAUDRATEPRESCALER_4);
    hspi1.State = HAL_SPI_STATE_READY; hspi1.ErrorCode = HAL_SPI_ERROR_NONE;
    DMA_HandleTypeDef *dma = hspi1.hdmatx;
    dma->Init.MemInc = DMA_MINC_ENABLE; dma->Init.Mode = DMA_NORMAL;
    dma->Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    dma->Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    dma->Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    dma->Init.Priority = DMA_PRIORITY_HIGH;
    if (HAL_DMA_Init(dma) != HAL_OK) return HMI_IO_ERROR;
    memset(lcd, 0, sizeof(*lcd)); owner = lcd;
    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RESET_GPIO_Port, RESET_Pin, GPIO_PIN_RESET);
    lcd->phase = RESET_LOW; lcd->deadline = HAL_GetTick() + 100U;
    return HMI_OK;
}
HmiTransport HmiSt7796_Transport(HmiSt7796 *lcd) { return (HmiTransport){lcd, Start, Poll}; }
bool HmiSt7796_Ready(const HmiSt7796 *lcd) { return lcd && lcd == owner && lcd->phase == READY; }
