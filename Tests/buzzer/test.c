#include "Buzzer.h"
#include <assert.h>
#include <stdio.h>
TimerRegs timer_regs;
static uint32_t tick,pin_mode,pin_level,clock_hz=32000000U,writes;
uint32_t HAL_GetTick(void) {return tick;}
uint32_t HAL_RCC_GetPCLK1Freq(void) {return clock_hz;}
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef *c,uint32_t *l) {c->APB1CLKDivider=2;c->APB2CLKDivider=1;*l=0;}
void HAL_GPIO_WritePin(void *p,uint32_t pin,GPIO_PinState v) {(void)p;assert(pin==8192U);pin_level=v;++writes;}
void HAL_GPIO_Init(void *p,GPIO_InitTypeDef *g) {(void)p;assert(g->Pull==GPIO_PULLDOWN);pin_mode=g->Mode;}
void HAL_NVIC_SetPriority(int irq,unsigned priority,unsigned sub) {assert(irq==TIM4_IRQn&&priority==8&&sub==0);}
void HAL_NVIC_ClearPendingIRQ(int irq) {assert(irq==TIM4_IRQn);}
void HAL_NVIC_EnableIRQ(int irq) {assert(irq==TIM4_IRQn);}
int main(void)
{
    assert(Buzzer_Play(2000,60)==BUZZER_NOT_INITIALIZED);
    assert(Buzzer_Init()==BUZZER_OK&&pin_mode==GPIO_MODE_OUTPUT_PP&&!pin_level);
    tick=UINT32_MAX-20;
    assert(Buzzer_Play(2000,60)==BUZZER_OK&&pin_mode==GPIO_MODE_OUTPUT_PP);
    assert(TIM4->PSC==63&&TIM4->ARR==249&&(TIM4->DIER&TIM_DIER_UIE));
    for(unsigned n=0;n<100;++n) {
        TIM4->SR=TIM_SR_UIF;Buzzer_TimerIRQ();assert(pin_level==((n+1U)&1U));
    }
    unsigned before=writes;TIM4->SR=0;Buzzer_TimerIRQ();assert(writes==before);
    tick+=59;Buzzer_Process();assert(Buzzer_IsPlaying());
    ++tick;Buzzer_Process();assert(!Buzzer_IsPlaying()&&!pin_level&&!TIM4->DIER&&!(TIM4->CR1&TIM_CR1_CEN));
    TIM4->SR=TIM_SR_UIF;Buzzer_TimerIRQ();assert(!pin_level);
    assert(Buzzer_Play(0,60)==BUZZER_INVALID_ARGUMENT);
    clock_hz=32000001U;assert(Buzzer_Play(2400,300)==BUZZER_TIMER_ERROR&&!pin_level);
    clock_hz=32000000U;assert(Buzzer_Play(10000,0)==BUZZER_OK&&TIM4->ARR==49);
    tick+=100000U;Buzzer_Process();assert(Buzzer_IsPlaying());assert(Buzzer_Stop()==BUZZER_OK&&!pin_level);
    assert(Buzzer_Play(20,1)==BUZZER_OK&&TIM4->ARR==24999);++tick;Buzzer_Process();assert(!Buzzer_IsPlaying());
    puts("PASS buzzer: dedicated TIM4/PB13 GPIO, IRQ edges, 1MHz clock, no TIM1 dependency, idle LOW, frequency limits and wrap-safe duration");
}