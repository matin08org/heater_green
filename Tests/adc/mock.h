#ifndef ADC_IO_TEST_MOCK_H
#define ADC_IO_TEST_MOCK_H
#define __MAIN_H
#include <stdint.h>
#include <stddef.h>
typedef enum {HAL_OK,HAL_ERROR} HAL_StatusTypeDef;
typedef struct {uint32_t CR;} DmaRegs;
typedef struct {uint32_t CR1;} TimerRegs;
typedef struct {uint32_t Channel,Direction,PeriphInc,MemInc,PeriphDataAlignment,MemDataAlignment,Mode,Priority,FIFOMode;} DmaInit;
typedef struct {DmaRegs *Instance;DmaInit Init;void *Parent;} DMA_HandleTypeDef;
typedef struct {uint32_t ClockPrescaler,ScanConvMode,ContinuousConvMode,ExternalTrigConvEdge,ExternalTrigConv,NbrOfConversion,DMAContinuousRequests,EOCSelection;} AdcInit;
typedef struct {AdcInit Init;DMA_HandleTypeDef *DMA_Handle;} ADC_HandleTypeDef;
typedef struct {uint32_t Channel,Rank,SamplingTime;} ADC_ChannelConfTypeDef;
typedef struct {uint32_t Prescaler,Period,CounterMode;} TimerInit;
typedef struct {TimerRegs *Instance;TimerInit Init;} TIM_HandleTypeDef;
typedef struct {uint32_t OCMode,Pulse,OCPolarity;} TIM_OC_InitTypeDef;
typedef struct {DMA_HandleTypeDef *hdmarx;uint32_t RxState;} UART_HandleTypeDef;
extern DmaRegs dma0,dma5;
extern TimerRegs timer5;
#define DMA2_Stream0 (&dma0)
#define DMA2_Stream5 (&dma5)
#define TIM5 (&timer5)
#define DMA_CHANNEL_0 0U
#define DMA_CHANNEL_4 4U
#define DMA_PERIPH_TO_MEMORY 0U
#define DMA_PINC_DISABLE 0U
#define DMA_MINC_ENABLE 1U
#define DMA_PDATAALIGN_HALFWORD 2U
#define DMA_MDATAALIGN_HALFWORD 2U
#define DMA_PDATAALIGN_BYTE 0U
#define DMA_MDATAALIGN_BYTE 0U
#define DMA_CIRCULAR 1U
#define DMA_PRIORITY_HIGH 2U
#define DMA_FIFOMODE_DISABLE 0U
#define DMA_SxCR_EN 1U
#define TIM_CR1_CEN 1U
#define TIM_COUNTERMODE_UP 0U
#define TIM_CHANNEL_1 0U
#define TIM_OCMODE_PWM1 0x60U
#define TIM_OCPOLARITY_HIGH 0U
#define ADC_CLOCK_SYNC_PCLK_DIV4 0x10000U
#define ADC_EXTERNALTRIGCONVEDGE_RISING 0x10000000U
#define ADC_EXTERNALTRIGCONV_T5_CC1 0x0a000000U
#define ADC_EOC_SEQ_CONV 0U
#define ADC_CHANNEL_2 2U
#define ADC_CHANNEL_3 3U
#define ADC_CHANNEL_4 4U
#define ADC_CHANNEL_5 5U
#define ADC_CHANNEL_VREFINT 17U
#define ADC_CHANNEL_TEMPSENSOR 16U
#define ADC_SAMPLETIME_480CYCLES 7U
#define ENABLE 1U
#define DISABLE 0U
#define DMA2_Stream0_IRQn 56
#define DMA2_Stream5_IRQn 68
#define ADC_IRQn 18
#define HAL_UART_STATE_READY 0U
#define HAL_UART_STATE_BUSY_RX 1U
#define __HAL_RCC_DMA2_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM5_CLK_ENABLE() ((void)0)
#define __HAL_LINKDMA(h,f,d) ((h)->f=&(d),(d).Parent=(h))
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t);
uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef*);
void HAL_DMA_IRQHandler(DMA_HandleTypeDef*);
HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef*);
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef*,ADC_ChannelConfTypeDef*);
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef*,uint32_t*,uint32_t);
HAL_StatusTypeDef HAL_TIM_PWM_Init(TIM_HandleTypeDef*);
HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef*,TIM_OC_InitTypeDef*,uint32_t);
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef*,uint32_t);
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef*,uint8_t*,uint16_t);
void HAL_NVIC_SetPriority(int,unsigned,unsigned);
void HAL_NVIC_EnableIRQ(int);
#endif
