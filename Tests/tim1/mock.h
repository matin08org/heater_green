#ifndef TIM1_PHASE_TEST_MOCK_H
#define TIM1_PHASE_TEST_MOCK_H
#define __MAIN_H
#include <stdint.h>
typedef enum {HAL_OK,HAL_ERROR} HAL_StatusTypeDef;
typedef struct {uint32_t CR1,PSC,ARR,CNT,CCR1,CCMR1,CCER,SMCR,DIER,EGR,SR,BDTR;} PhaseTimer;
typedef struct {uint32_t Prescaler,CounterMode,Period,ClockDivision,RepetitionCounter,AutoReloadPreload;} TimerInit;
typedef struct {PhaseTimer *Instance;TimerInit Init;} TIM_HandleTypeDef;
typedef struct {uint32_t MasterOutputTrigger,MasterSlaveMode;} TIM_MasterConfigTypeDef;
typedef struct {uint32_t OCMode,Pulse,OCPolarity,OCNPolarity,OCFastMode,OCIdleState,OCNIdleState;} TIM_OC_InitTypeDef;
typedef struct {uint32_t ICPolarity,ICSelection,ICPrescaler,ICFilter;} TIM_IC_InitTypeDef;
typedef struct {uint32_t SlaveMode,InputTrigger,TriggerPolarity,TriggerPrescaler,TriggerFilter;} TIM_SlaveConfigTypeDef;
typedef struct {uint32_t OffStateRunMode,OffStateIDLEMode,LockLevel,DeadTime,BreakState,BreakPolarity,AutomaticOutput;} TIM_BreakDeadTimeConfigTypeDef;
typedef struct {uint32_t Pin,Mode,Pull,Speed,Alternate;} GPIO_InitTypeDef;
typedef struct {uint32_t APB2CLKDivider;} RCC_ClkInitTypeDef;
extern PhaseTimer phase_timer,timer6;
#define TIM1 (&phase_timer)
#define TIM6 (&timer6)
#define GPIOA ((void*)1)
#define GPIOB ((void*)2)
#define fan_ehtragh_GPIO_Port GPIOA
#define fan_ehtragh_Pin (1U<<8)
#define zc_Pin (1U<<9)
#define TIM_COUNTERMODE_UP 0U
#define TIM_CLOCKDIVISION_DIV1 0U
#define TIM_AUTORELOAD_PRELOAD_DISABLE 0U
#define TIM_TRGO_RESET 0U
#define TIM_MASTERSLAVEMODE_DISABLE 0U
#define TIM_OCMODE_PWM2 0x70U
#define TIM_OCPOLARITY_HIGH 0U
#define TIM_OCNPOLARITY_HIGH 0U
#define TIM_OCFAST_DISABLE 0U
#define TIM_OCIDLESTATE_RESET 0U
#define TIM_OCNIDLESTATE_RESET 0U
#define TIM_INPUTCHANNELPOLARITY_RISING 0U
#define TIM_ICSELECTION_DIRECTTI 1U
#define TIM_ICPSC_DIV1 0U
#define TIM_SLAVEMODE_RESET 4U
#define TIM_TS_TI2FP2 0x60U
#define TIM_TRIGGERPOLARITY_RISING 0U
#define TIM_TRIGGERPRESCALER_DIV1 0U
#define TIM_OSSR_DISABLE 0U
#define TIM_OSSI_DISABLE 0U
#define TIM_LOCKLEVEL_OFF 0U
#define TIM_BREAK_DISABLE 0U
#define TIM_BREAKPOLARITY_HIGH 0U
#define TIM_AUTOMATICOUTPUT_DISABLE 0U
#define TIM_CHANNEL_1 0U
#define TIM_CHANNEL_2 4U
#define TIM_CR1_URS 4U
#define TIM_EGR_UG 1U
#define TIM_FLAG_UPDATE 1U
#define RCC_HCLK_DIV1 1U
#define GPIO_MODE_AF_PP 2U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_LOW 0U
#define GPIO_AF1_TIM1 1U
#define TIM6_IRQn 54
#define SET_BIT(r,b) ((r)|=(b))
#define __HAL_TIM_SET_COUNTER(t,v) ((t)->Instance->CNT=(v))
#define __HAL_TIM_SET_COMPARE(t,c,v) ((void)(c),(t)->Instance->CCR1=(v))
#define __HAL_TIM_CLEAR_FLAG(t,v) ((t)->Instance->SR&=~(v))
#define __HAL_RCC_TIM1_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM6_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM1_CLK_DISABLE() ((void)0)
#define __HAL_RCC_TIM6_CLK_DISABLE() ((void)0)
void Error_Handler(void);
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef*,uint32_t*);
uint32_t HAL_RCC_GetPCLK2Freq(void);
void HAL_GPIO_Init(void*,GPIO_InitTypeDef*);
void HAL_GPIO_DeInit(void*,uint32_t);
void HAL_NVIC_SetPriority(int,unsigned,unsigned);
void HAL_NVIC_EnableIRQ(int);
void HAL_NVIC_DisableIRQ(int);
void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef*);
HAL_StatusTypeDef HAL_TIM_PWM_Init(TIM_HandleTypeDef*);
HAL_StatusTypeDef HAL_TIM_IC_Init(TIM_HandleTypeDef*);
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef*);
HAL_StatusTypeDef HAL_TIMEx_MasterConfigSynchronization(TIM_HandleTypeDef*,TIM_MasterConfigTypeDef*);
HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef*,TIM_OC_InitTypeDef*,uint32_t);
HAL_StatusTypeDef HAL_TIM_IC_ConfigChannel(TIM_HandleTypeDef*,TIM_IC_InitTypeDef*,uint32_t);
HAL_StatusTypeDef HAL_TIM_SlaveConfigSynchro(TIM_HandleTypeDef*,TIM_SlaveConfigTypeDef*);
HAL_StatusTypeDef HAL_TIMEx_ConfigBreakDeadTime(TIM_HandleTypeDef*,TIM_BreakDeadTimeConfigTypeDef*);
HAL_StatusTypeDef HAL_TIM_IC_Start(TIM_HandleTypeDef*,uint32_t);
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef*,uint32_t);
#endif
