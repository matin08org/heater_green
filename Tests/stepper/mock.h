#define __MAIN_H
#include <stdint.h>
typedef enum {HAL_OK,HAL_ERROR,HAL_BUSY} HAL_StatusTypeDef;
typedef enum {GPIO_PIN_RESET,GPIO_PIN_SET} GPIO_PinState;
typedef struct {uint32_t CR1,CR2,CNT,ARR,SR,PSC,CCR3,EGR,SMCR,DIER,CCMR2,CCER;} Timer;
extern Timer mock_tim2,mock_tim3;
#define TIM2 (&mock_tim2)
#define TIM3 (&mock_tim3)
#define TIM_CR1_CEN 1
#define TIM_CR1_OPM 8
#define TIM_CR1_URS 4
#define TIM_SR_UIF 1
#define TIM_EGR_UG 1
#define TIM_CCER_CC3E 256
#define TIM_TRGO_UPDATE 32
#define TIM_TRGO_ENABLE 16
#define TIM_TS_ITR1 16
#define TIM_TS_ITR2 32
#define TIM_SLAVEMODE_EXTERNAL1 7
#define TIM_SLAVEMODE_GATED 5
#define TIM_DIER_UIE 1
#define TIM_OCMODE_PWM2 112
#define RCC_HCLK_DIV1 0
#define TIM2_IRQn 28
#define GPIOC ((void*)1)
#define ENSTEP1_Pin 128
#define ENSTEP2_Pin 64
#define ENSTEP2_GPIO_Port GPIOC
#define DIR_Pin 512
#define DIR_GPIO_Port GPIOC
#define STEP_Pin 256
#define STEP_GPIO_Port GPIOC
#define GPIO_MODE_OUTPUT_PP 1
#define GPIO_MODE_AF_PP 2
#define GPIO_NOPULL 0
#define GPIO_SPEED_FREQ_LOW 0
#define GPIO_SPEED_FREQ_VERY_HIGH 3
#define GPIO_AF2_TIM3 2
typedef struct {uint32_t Pin,Mode,Pull,Speed,Alternate;} GPIO_InitTypeDef;
typedef struct {uint32_t APB1CLKDivider;} RCC_ClkInitTypeDef;
#define __HAL_RCC_GPIOC_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM2_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM3_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM2_FORCE_RESET() ((void)0)
#define __HAL_RCC_TIM3_FORCE_RESET() ((void)0)
#define __HAL_RCC_TIM2_RELEASE_RESET() ((void)0)
#define __HAL_RCC_TIM3_RELEASE_RESET() ((void)0)
static inline uint32_t __get_PRIMASK(void) {return 0;}
static inline void __disable_irq(void) {}
static inline void __set_PRIMASK(uint32_t p) {(void)p;}
void HAL_GPIO_WritePin(void*,uint32_t,GPIO_PinState);
void HAL_GPIO_Init(void*,GPIO_InitTypeDef*);
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef*,uint32_t*);
uint32_t HAL_RCC_GetPCLK1Freq(void);
void HAL_Delay(uint32_t);
uint32_t HAL_GetTick(void);
void HAL_NVIC_ClearPendingIRQ(int);
void HAL_NVIC_SetPriority(int,int,int);
void HAL_NVIC_EnableIRQ(int);
