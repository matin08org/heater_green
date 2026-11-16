#include "HmiAssetDma.h"
#include <assert.h>
#include <stdio.h>
AssetMockStream asset_stream;
AssetMockDma asset_controller;
AssetMockTimer asset_timer;
static uint32_t tick,clock_hz=64000000U;
uint32_t HAL_GetTick(void) {return tick;}
uint32_t HAL_RCC_GetPCLK2Freq(void) {return clock_hz;}
void HAL_NVIC_SetPriority(int irq,unsigned priority,unsigned sub)
{assert(irq==DMA2_Stream1_IRQn&&priority==6&&sub==0);}
void HAL_NVIC_EnableIRQ(int irq) {assert(irq==DMA2_Stream1_IRQn);}
static const uint8_t *Flash(uint32_t offset) {return (const uint8_t *)(uintptr_t)(0x90000000U+offset);}
static uint8_t *Ram(uint32_t offset) {return (uint8_t *)(uintptr_t)(0x20000000U+offset);}
static void Complete(void)
{
    DMA2_Stream1->NDTR=0;DMA2_Stream1->CR&=~DMA_SxCR_EN;DMA2->LISR=DMA_LISR_TCIF1;HmiAssetDma_IRQHandler();
    assert(!(TIM8->CR1&TIM_CR1_CEN)&&!TIM8->DIER);
}
int main(void)
{
    HmiAssetDma d;
    clock_hz=32000000U;assert(HmiAssetDma_Init(&d)==HMI_INVALID);clock_hz=64000000U;
    assert(HmiAssetDma_Init(&d)==HMI_OK);HmiAssetIo io=HmiAssetDma_Io(&d);
    assert(TIM8->ARR==15&&TIM8->PSC==0&&!TIM8->DIER);
    assert(io.start(&d,Flash(1),Ram(0x1000),13)==HMI_OK);
    assert(DMA2_Stream1->M0AR==0x90000001U&&DMA2_Stream1->PAR==0x20001000U);
    assert((DMA2_Stream1->CR&(3U<<6))==DMA_SxCR_DIR_0); /* QSPI on MEMORY port */
    assert((DMA2_Stream1->CR>>25)==7U&&DMA2_Stream1->NDTR==13);
    assert(!(DMA2_Stream1->CR&(DMA_SxCR_MSIZE_1|DMA_SxCR_PSIZE_1)));
    assert((DMA2_Stream1->CR&DMA_SxCR_MINC)&&(DMA2_Stream1->CR&DMA_SxCR_PINC));
    assert(!TIM8->DIER&&!(TIM8->CR1&TIM_CR1_CEN)&&!d.armed);
    /* DMA not yet prefetched: timer must not consume an empty QSPI FIFO. */
    DMA2_Stream1->FCR=7U|(4U<<3);
    assert(io.poll(&d)==HMI_BUSY&&!TIM8->DIER);
    DMA2_Stream1->FCR=7U|(5U<<3);
    assert(io.poll(&d)==HMI_BUSY&&d.armed);
    assert(TIM8->DIER==TIM_DIER_UDE&&(TIM8->CR1&TIM_CR1_CEN));
    /* A stale TC flag cannot finish a new, nonzero-NDTR transfer. */
    DMA2->LISR=DMA_LISR_TCIF1;HmiAssetDma_IRQHandler();assert(!d.done&&DMA2_Stream1->NDTR==13);
    DMA2->LISR=0;
    assert(io.start(&d,Flash(4),Ram(0x2000),16)==HMI_BUSY&&io.poll(&d)==HMI_BUSY);
    Complete();assert(d.done&&d.busy);assert(io.poll(&d)==HMI_OK&&!d.busy&&d.completed==1);
    DMA2->LISR=0;
    assert(io.start(&d,Flash(4),Ram(0x2000),16)==HMI_OK);
    assert(DMA2_Stream1->NDTR==4&&(DMA2_Stream1->CR&DMA_SxCR_MSIZE_1));
    /* FIFO underrun retries; it is not a terminal error or a completed copy. */
    DMA2->LISR=DMA_LISR_FEIF1;HmiAssetDma_IRQHandler();assert(!d.failed&&d.fifo_events==1);
    DMA2->LISR=0;Complete();assert(io.poll(&d)==HMI_OK);DMA2->LISR=0;
    assert(io.start(&d,Flash(0x1fffff),Ram(0x1000),2)==HMI_INVALID);
    assert(io.start(&d,Flash(0),Ram(0x3ffff),2)==HMI_INVALID);
    assert(io.start(&d,Flash(0),Ram(0x1000),0)==HMI_INVALID);
    /* IRQ error does no rendering and leaves ownership latched on failure. */
    assert(io.start(&d,Flash(0),Ram(0x1000),100)==HMI_OK);
    DMA2->LISR=DMA_LISR_TEIF1;HmiAssetDma_IRQHandler();
    assert(io.poll(&d)==HMI_IO_ERROR&&d.errors==1&&d.busy&&!TIM8->DIER);
    assert(HmiAssetDma_Init(&d)==HMI_OK);
    DMA2->LISR=0;
    tick=0xfffffff0U;assert(io.start(&d,Flash(0),Ram(0x1000),100)==HMI_OK);
    tick+=99U;assert(io.poll(&d)==HMI_BUSY);++tick;
    assert(io.poll(&d)==HMI_IO_ERROR&&d.errors==1&&!TIM8->DIER);
    puts("PASS asset DMA registers: FIFO prefill before TIM8, full/empty states, transient underrun, stale TC rejection, ports/widths, ownership, bounds, error and timeout");
    return 0;
}
