#ifndef HMI_ASSET_TEST_MOCK_H
#define HMI_ASSET_TEST_MOCK_H
#define __MAIN_H
#include <stdint.h>
typedef struct {uint32_t CR,NDTR,PAR,M0AR,FCR;} AssetMockStream;
typedef struct {uint32_t LISR,LIFCR;} AssetMockDma;
typedef struct {uint32_t CR1,DIER,PSC,ARR,RCR,EGR,SR,CNT;} AssetMockTimer;
extern AssetMockStream asset_stream;
extern AssetMockDma asset_controller;
extern AssetMockTimer asset_timer;
#define DMA2_Stream1 (&asset_stream)
#define DMA2 (&asset_controller)
#define TIM8 (&asset_timer)
#define DMA_SxCR_EN 1U
#define DMA_SxCR_DMEIE 2U
#define DMA_SxCR_TEIE 4U
#define DMA_SxCR_TCIE 16U
#define DMA_SxCR_DIR_0 (1U<<6)
#define DMA_SxCR_PINC (1U<<9)
#define DMA_SxCR_MINC (1U<<10)
#define DMA_SxCR_PSIZE_1 (2U<<11)
#define DMA_SxCR_MSIZE_1 (2U<<13)
#define DMA_SxCR_PL_0 (1U<<16)
#define DMA_SxCR_CHSEL_Pos 25U
#define DMA_SxFCR_DMDIS 4U
#define DMA_SxFCR_FTH 3U
#define DMA_SxFCR_FEIE 128U
#define DMA_SxFCR_FS (7U<<3)
#define DMA_SxFCR_FS_Pos 3U
#define DMA_LISR_FEIF1 (1U<<6)
#define DMA_LISR_DMEIF1 (1U<<8)
#define DMA_LISR_TEIF1 (1U<<9)
#define DMA_LISR_TCIF1 (1U<<11)
#define DMA_LIFCR_CFEIF1 DMA_LISR_FEIF1
#define DMA_LIFCR_CDMEIF1 DMA_LISR_DMEIF1
#define DMA_LIFCR_CTEIF1 DMA_LISR_TEIF1
#define DMA_LIFCR_CHTIF1 (1U<<10)
#define DMA_LIFCR_CTCIF1 DMA_LISR_TCIF1
#define TIM_CR1_CEN 1U
#define TIM_CR1_URS 4U
#define TIM_EGR_UG 1U
#define TIM_DIER_UDE (1U<<8)
#define DMA2_Stream1_IRQn 57
#define CLEAR_BIT(r,b) ((r)&=~(b))
#define SET_BIT(r,b) ((r)|=(b))
#define __HAL_RCC_DMA2_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM8_CLK_ENABLE() ((void)0)
#define __DMB() ((void)0)
uint32_t HAL_GetTick(void);
uint32_t HAL_RCC_GetPCLK2Freq(void);
void HAL_NVIC_SetPriority(int,unsigned,unsigned);
void HAL_NVIC_EnableIRQ(int);
#endif
