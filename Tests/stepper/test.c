#include "DamperStepper.h"
#include <assert.h>
#include <stdio.h>
Timer mock_tim2,mock_tim3;
static uint32_t pins;
static uint32_t tick;
uint32_t HAL_GetTick(void) {return tick;}
void HAL_GPIO_WritePin(void *p,uint32_t pin,GPIO_PinState s) {(void)p;if(s)pins|=pin;else pins&=~pin;}
void HAL_GPIO_Init(void *p,GPIO_InitTypeDef *g) {(void)p;(void)g;}
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef *c,uint32_t *l) {c->APB1CLKDivider=1;*l=0;}
uint32_t HAL_RCC_GetPCLK1Freq(void) {return 32000000;}
void HAL_Delay(uint32_t ms) {assert(ms==2);}
void HAL_NVIC_ClearPendingIRQ(int i) {(void)i;}
void HAL_NVIC_SetPriority(int i,int p,int s) {(void)i;(void)p;(void)s;}
void HAL_NVIC_EnableIRQ(int i) {(void)i;}
int main(void)
{
    assert(DamperStepper_Init()==HAL_OK && !DamperStepper_IsRunning());
    assert((pins&192)==192 && TIM3->PSC==3 && TIM3->ARR==63999);
    assert(DamperStepper_SetEndpoint(16,DAMPER_END_STOP)==HAL_OK);
    assert(DamperStepper_Start()==HAL_OK && !(pins&64) && !(TIM3->CR1&1));
    tick+=1;DamperStepper_Process();assert(!(TIM3->CR1&1));
    tick+=1;DamperStepper_Process();assert(TIM2->ARR==16 && TIM2->CNT==1 && (TIM3->CR1&1));
    assert(DamperStepper_SetSpeedRPM(10)==HAL_BUSY);
    TIM2->CNT=0;TIM2->SR=TIM_SR_UIF;DamperStepper_EndpointIRQHandler();
    assert(!DamperStepper_IsRunning() && DamperStepper_GetPosition()==16 && (pins&64));
    assert(DamperStepper_SetEndpoint(15,DAMPER_END_REVERSE)==HAL_OK);
    assert(DamperStepper_Start()==HAL_OK);
    tick+=2;DamperStepper_Process();assert(TIM2->ARR==1 && !(pins&DIR_Pin));
    TIM2->CNT=0;TIM2->SR=TIM_SR_UIF;DamperStepper_EndpointIRQHandler();
    assert(DamperStepper_IsRunning() && (pins&DIR_Pin));
    DamperStepper_Stop();
    assert(!DamperStepper_IsRunning() && (pins&64) && !(TIM2->CR1&1) && !(TIM3->CR1&1));
    TIM2->SR=TIM_SR_UIF;DamperStepper_EndpointIRQHandler();
    assert(!DamperStepper_IsRunning() && !(TIM3->CR1&1));
    assert(DamperStepper_SetEndpoint(100,DAMPER_END_STOP)==HAL_OK);
    assert(DamperStepper_Start()==HAL_OK);DamperStepper_Stop();
    tick+=20;DamperStepper_Process();assert(!DamperStepper_IsRunning() && !(TIM3->CR1&1));
    puts("PASS: real stepper init, frequency, active-LOW PC6, endpoint/one pulse/reverse, abort and pending IRQ; register simulation only");
}
