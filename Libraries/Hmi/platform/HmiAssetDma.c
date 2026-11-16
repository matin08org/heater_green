#include "HmiAssetDma.h"
#include "main.h"
#include <string.h>
static HmiAssetDma *owner;
#define FLAGS (DMA_LIFCR_CFEIF1|DMA_LIFCR_CDMEIF1|DMA_LIFCR_CTEIF1|DMA_LIFCR_CHTIF1|DMA_LIFCR_CTCIF1)
static void Stop(void)
{
    TIM8->DIER=0;CLEAR_BIT(TIM8->CR1,TIM_CR1_CEN);
}
HmiResult HmiAssetDma_Init(HmiAssetDma *d)
{
    if(!d)return HMI_INVALID;
    __HAL_RCC_DMA2_CLK_ENABLE();__HAL_RCC_TIM8_CLK_ENABLE();
    if((DMA2_Stream1->CR&DMA_SxCR_EN)||(TIM8->CR1&TIM_CR1_CEN)||(owner&&owner!=d))return HMI_BUSY;
    if(HAL_RCC_GetPCLK2Freq()!=64000000U)return HMI_INVALID;
    memset(d,0,sizeof(*d));owner=d;
    Stop();TIM8->CR1=TIM_CR1_URS;TIM8->PSC=0;TIM8->ARR=15;TIM8->RCR=0;
    TIM8->EGR=TIM_EGR_UG;TIM8->SR=0;TIM8->CNT=0;
    DMA2_Stream1->CR=0;DMA2_Stream1->FCR=DMA_SxFCR_DMDIS|DMA_SxFCR_FTH;
    DMA2->LIFCR=FLAGS;
    HAL_NVIC_SetPriority(DMA2_Stream1_IRQn,6,0);HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);
    return HMI_OK;
}
static HmiResult Start(void *context,const uint8_t *src,uint8_t *dst,size_t bytes)
{
    HmiAssetDma *d=context;
    uintptr_t from=(uintptr_t)src,to=(uintptr_t)dst;
    if(!d||d!=owner||!bytes||bytes>65535U||from<0x90000000U||from>=0x90200000U||
       bytes>0x90200000U-from||to<0x20000000U||to>=0x20040000U||bytes>0x20040000U-to)return HMI_INVALID;
    if(d->failed)return HMI_IO_ERROR;
    if(d->busy||(DMA2_Stream1->CR&DMA_SxCR_EN))return HMI_BUSY;
    Stop();DMA2->LIFCR=FLAGS;
    bool words=((from|to|bytes)&3U)==0;
    uint32_t width=words?(DMA_SxCR_MSIZE_1|DMA_SxCR_PSIZE_1):0U;
    /* DIR=M2P is deliberate: memory port READS QSPI; peripheral port WRITES
     * ordinary SRAM. TIM8 only generates DMA requests, not data or interrupts. */
    DMA2_Stream1->CR=(7UL<<DMA_SxCR_CHSEL_Pos)|DMA_SxCR_DIR_0|DMA_SxCR_MINC|DMA_SxCR_PINC|
        DMA_SxCR_PL_0|DMA_SxCR_TCIE|DMA_SxCR_TEIE|DMA_SxCR_DMEIE|width;
    DMA2_Stream1->M0AR=(uint32_t)from;DMA2_Stream1->PAR=(uint32_t)to;
    DMA2_Stream1->NDTR=(uint32_t)(words?bytes/4U:bytes);
    DMA2_Stream1->FCR=DMA_SxFCR_DMDIS|DMA_SxFCR_FTH;
    d->done=0;d->busy=true;d->armed=false;d->started=HAL_GetTick();d->bytes+=(uint32_t)bytes;
    TIM8->CNT=0;TIM8->SR=0;
    __DMB();SET_BIT(DMA2_Stream1->CR,DMA_SxCR_EN);
    /* Enabling a QSPI-backed stream does not make its FIFO ready immediately.
     * Starting a 4-MHz request source here can underrun the first transfer.
     * Poll waits asynchronously for prefetch; never spin inside Start/IRQ. */
    return HMI_OK;
}
void HmiAssetDma_IRQHandler(void)
{
    uint32_t flags=DMA2->LISR;DMA2->LIFCR=FLAGS;
    if(!owner||!owner->busy)return;
    owner->last_flags=flags;owner->last_ndtr=DMA2_Stream1->NDTR;
    if(flags&DMA_LISR_FEIF1)++owner->fifo_events;
    if(flags&(DMA_LISR_TEIF1|DMA_LISR_DMEIF1)) {
        Stop();CLEAR_BIT(DMA2_Stream1->CR,DMA_SxCR_EN);++owner->errors;__DMB();owner->failed=1;
    } else if((flags&DMA_LISR_TCIF1)&&DMA2_Stream1->NDTR==0U) {
        Stop();__DMB();owner->done=1;
    }
    /* Only signal completion. No flash reads, rendering, HAL waits or SPI work
     * inside IRQ; the cooperative compositor handles a bounded strip next. */
}
static HmiResult Poll(void *context)
{
    HmiAssetDma *d=context;if(!d||d!=owner)return HMI_INVALID;
    if(d->failed)return HMI_IO_ERROR;
    if(!d->busy)return HMI_OK;
    uint32_t flags=DMA2->LISR;
    if(flags&(DMA_LISR_TEIF1|DMA_LISR_DMEIF1)) {
        HmiAssetDma_IRQHandler();return HMI_IO_ERROR;
    }
    /* A transient M2P FIFO underrun retries on a later timer request; it does
     * not lose a source sample (RM0402 DMA interrupts). Record, do not abort. */
    if(flags&DMA_LISR_FEIF1) {
        d->last_flags=flags;d->last_ndtr=DMA2_Stream1->NDTR;++d->fifo_events;
        DMA2->LIFCR=DMA_LIFCR_CFEIF1;
    }
    if(!d->armed) {
        uint32_t fill=(DMA2_Stream1->FCR&DMA_SxFCR_FS)>>DMA_SxFCR_FS_Pos;
        if(fill<=5U&&fill!=4U) { /* 4=empty, 5=full; partial is valid for short strips */
            d->armed=true;TIM8->CNT=0;TIM8->SR=0;
            TIM8->DIER=TIM_DIER_UDE;SET_BIT(TIM8->CR1,TIM_CR1_CEN);
        }
    }
    /* TC may be pending even if another IRQ delayed its handler. A full copy
     * is complete only after NDTR=0 and hardware has released EN. */
    if((flags&DMA_LISR_TCIF1)&&DMA2_Stream1->NDTR==0U) {
        Stop();d->done=1;DMA2->LIFCR=DMA_LIFCR_CTCIF1;
    }
    if(d->done&&!(DMA2_Stream1->CR&DMA_SxCR_EN)&&DMA2_Stream1->NDTR==0U) {
        __DMB();d->busy=false;d->done=0;++d->completed;return HMI_OK;
    }
    if((uint32_t)(HAL_GetTick()-d->started)>=100U) {
        d->last_flags=flags;d->last_ndtr=DMA2_Stream1->NDTR;
        Stop();CLEAR_BIT(DMA2_Stream1->CR,DMA_SxCR_EN);d->failed=1;++d->errors;return HMI_IO_ERROR;
    }
    return HMI_BUSY;
}
bool HmiAssetDma_Quiescent(const HmiAssetDma *d)
{return d&&d==owner&&!(DMA2_Stream1->CR&DMA_SxCR_EN)&&!(TIM8->CR1&TIM_CR1_CEN);}
HmiAssetIo HmiAssetDma_Io(HmiAssetDma *d) {return (HmiAssetIo){d,Start,Poll};}
