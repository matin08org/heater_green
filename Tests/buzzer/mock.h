#ifndef BUZZER_TEST_MOCK_H
#define BUZZER_TEST_MOCK_H
#define __MAIN_H
#include <stdint.h>
typedef enum {HAL_OK,HAL_ERROR} HAL_StatusTypeDef;
typedef enum {GPIO_PIN_RESET,GPIO_PIN_SET} GPIO_PinState;
typedef struct {uint32_t CR1,DIER,PSC,ARR,CNT,EGR,SR;} TimerRegs;
typedef struct {uint32_t Pin,Mode,Pull,Speed,Alternate;} GPIO_InitTypeDef;
typedef struct {uint32_t APB1CLKDivider,APB2CLKDivider;} RCC_ClkInitTypeDef;
extern TimerRegs timer_regs;
#define TIM4 (&timer_regs)
#define TIM_CR1_CEN 1U
#define TIM_CR1_URS 4U
#define TIM_SR_UIF 1U
#define TIM_DIER_UIE 1U
#define TIM_EGR_UG 1U
#define TIM4_IRQn 30
#define RCC_HCLK_DIV1 1U
#define GPIO_MODE_OUTPUT_PP 1U
#define GPIO_PULLDOWN 2U
#define GPIO_SPEED_FREQ_LOW 0U
#define buzzer_Pin 8192U
#define buzzer_GPIO_Port ((void*)1)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM4_CLK_ENABLE() ((void)0)
#define CLEAR_BIT(r,b) ((r)&=~(b))
#define SET_BIT(r,b) ((r)|=(b))
static inline uint32_t __get_PRIMASK(void) {return 0;}
static inline void __disable_irq(void) {}
static inline void __set_PRIMASK(uint32_t p) {(void)p;}
uint32_t HAL_GetTick(void);
uint32_t HAL_RCC_GetPCLK1Freq(void);
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef*,uint32_t*);
void HAL_GPIO_WritePin(void*,uint32_t,GPIO_PinState);
void HAL_GPIO_Init(void*,GPIO_InitTypeDef*);
void HAL_NVIC_SetPriority(int,unsigned,unsigned);
void HAL_NVIC_ClearPendingIRQ(int);
void HAL_NVIC_EnableIRQ(int);
#endif