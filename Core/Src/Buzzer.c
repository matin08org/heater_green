#include "Buzzer.h"
/* PB13 only exposes TIM1_CH1N. TIM4 provides a short half-period IRQ so
 * TIM1 remains exclusively owned by PA8/PA9 without changing board wiring. */
static uint8_t initialized;
static volatile uint8_t playing,level;
static uint32_t started,duration;
static uint32_t TimerClock(void)
{
    RCC_ClkInitTypeDef c;uint32_t latency;
    HAL_RCC_GetClockConfig(&c,&latency);
    uint32_t hz=HAL_RCC_GetPCLK1Freq();
    if(c.APB1CLKDivider!=RCC_HCLK_DIV1)hz*=2U;
    return hz;
}
Buzzer_Status_t Buzzer_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();__HAL_RCC_TIM4_CLK_ENABLE();
    if(TIM4->CR1&TIM_CR1_CEN)return BUZZER_TIMER_ERROR;
    GPIO_InitTypeDef gpio={0};
    HAL_GPIO_WritePin(buzzer_GPIO_Port,buzzer_Pin,GPIO_PIN_RESET);
    gpio.Pin=buzzer_Pin;gpio.Mode=GPIO_MODE_OUTPUT_PP;gpio.Pull=GPIO_PULLDOWN;
    gpio.Speed=GPIO_SPEED_FREQ_LOW;HAL_GPIO_Init(buzzer_GPIO_Port,&gpio);
    TIM4->CR1=TIM_CR1_URS;TIM4->DIER=0;TIM4->SR=0;
    HAL_NVIC_SetPriority(TIM4_IRQn,8,0);HAL_NVIC_ClearPendingIRQ(TIM4_IRQn);HAL_NVIC_EnableIRQ(TIM4_IRQn);
    playing=level=0;started=duration=0;initialized=1;return BUZZER_OK;
}
Buzzer_Status_t Buzzer_Play(uint32_t frequency_hz,uint32_t duration_ms)
{
    if(!initialized)return BUZZER_NOT_INITIALIZED;
    if(frequency_hz<BUZZER_MIN_FREQUENCY_HZ||frequency_hz>BUZZER_MAX_FREQUENCY_HZ)return BUZZER_INVALID_ARGUMENT;
    uint32_t hz=TimerClock(),half=BUZZER_COUNTER_FREQUENCY_HZ/(2U*frequency_hz);
    if(!half||half>65536U||hz<BUZZER_COUNTER_FREQUENCY_HZ||hz%BUZZER_COUNTER_FREQUENCY_HZ||
       hz/BUZZER_COUNTER_FREQUENCY_HZ>65536U)return BUZZER_TIMER_ERROR;
    uint32_t mask=__get_PRIMASK();__disable_irq();
    playing=0;TIM4->DIER=0;CLEAR_BIT(TIM4->CR1,TIM_CR1_CEN);
    HAL_GPIO_WritePin(buzzer_GPIO_Port,buzzer_Pin,GPIO_PIN_RESET);level=0;
    TIM4->PSC=hz/BUZZER_COUNTER_FREQUENCY_HZ-1U;TIM4->ARR=half-1U;
    TIM4->CNT=0;TIM4->EGR=TIM_EGR_UG;TIM4->SR=0;
    started=HAL_GetTick();duration=duration_ms;playing=1;
    HAL_NVIC_ClearPendingIRQ(TIM4_IRQn);
    TIM4->DIER=TIM_DIER_UIE;SET_BIT(TIM4->CR1,TIM_CR1_CEN);
    __set_PRIMASK(mask);return BUZZER_OK;
}
Buzzer_Status_t Buzzer_Stop(void)
{
    if(!initialized)return BUZZER_NOT_INITIALIZED;
    uint32_t mask=__get_PRIMASK();__disable_irq();
    playing=0;TIM4->DIER=0;CLEAR_BIT(TIM4->CR1,TIM_CR1_CEN);TIM4->SR=0;
    HAL_GPIO_WritePin(buzzer_GPIO_Port,buzzer_Pin,GPIO_PIN_RESET);level=0;duration=0;
    HAL_NVIC_ClearPendingIRQ(TIM4_IRQn);__set_PRIMASK(mask);return BUZZER_OK;
}
void Buzzer_TimerIRQ(void)
{
    if(!(TIM4->SR&TIM_SR_UIF))return;
    TIM4->SR=~TIM_SR_UIF;
    if(!playing||!(TIM4->DIER&TIM_DIER_UIE))return;
    level^=1U;
    HAL_GPIO_WritePin(buzzer_GPIO_Port,buzzer_Pin,level?GPIO_PIN_SET:GPIO_PIN_RESET);
}
void Buzzer_Process(void)
{if(playing&&duration&&(uint32_t)(HAL_GetTick()-started)>=duration)(void)Buzzer_Stop();}
uint8_t Buzzer_IsPlaying(void) {return playing;}