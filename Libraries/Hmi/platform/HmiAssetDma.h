#ifndef HMI_ASSET_DMA_H
#define HMI_ASSET_DMA_H
#include "HmiPagesDma.h"
/* Dedicated DMA2 Stream1/channel7, paced by internal TIM8_UP. No GPIO/timer IRQ.
 * QSPI is read through M0AR (AHB MEMORY port), SRAM written through PAR.
 * This avoids ES0305 2.2.11 during concurrent ADC/SPI1/USART1 DMA activity. */
typedef struct {
    volatile uint8_t done,failed;
    bool busy,armed;
    uint32_t started,completed,bytes,errors;
    uint32_t last_flags,last_ndtr,fifo_events;
} HmiAssetDma;
HmiResult HmiAssetDma_Init(HmiAssetDma *dma);
HmiAssetIo HmiAssetDma_Io(HmiAssetDma *dma);
void HmiAssetDma_IRQHandler(void);
bool HmiAssetDma_Quiescent(const HmiAssetDma *dma);
#endif
