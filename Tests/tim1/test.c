#include "tim.h"
#include <assert.h>
#include <stdio.h>
PhaseTimer phase_timer,timer6;
static uint32_t pins;
static TIM_SlaveConfigTypeDef slave;
static TIM_IC_InitTypeDef capture;
void Error_Handler(void) {assert(0);}
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef *c,uint32_t *l) {c->APB2CLKDivider=1;*l=0;}
uint32_t HAL_RCC_GetPCLK2Freq(void) {return 64000000U;}
void HAL_GPIO_Init(void *p,GPIO_InitTypeDef *g)
{
    assert(p==GPIOA&&g->Mode==GPIO_MODE_AF_PP&&g->Pull==GPIO_NOPULL&&g->Alternate==1U);
    assert((g->Pin&~(fan_ehtragh_Pin|zc_Pin))==0);pins|=g->Pin;
}
void HAL_GPIO_DeInit(void *p,uint32_t pin) {(void)p;(void)pin;}
void HAL_NVIC_SetPriority(int i,unsigned p,unsigned s) {(void)i;(void)p;(void)s;}
void HAL_NVIC_EnableIRQ(int i) {(void)i;}
void HAL_NVIC_DisableIRQ(int i) {(void)i;}
HAL_StatusTypeDef HAL_TIM_PWM_Init(TIM_HandleTypeDef *t)
{t->Instance->PSC=t->Init.Prescaler;t->Instance->ARR=t->Init.Period;HAL_TIM_PWM_MspInit(t);return HAL_OK;}
HAL_StatusTypeDef HAL_TIM_IC_Init(TIM_HandleTypeDef *t) {(void)t;return HAL_OK;}
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *t) {(void)t;return HAL_OK;}
HAL_StatusTypeDef HAL_TIMEx_MasterConfigSynchronization(TIM_HandleTypeDef *t,TIM_MasterConfigTypeDef *c) {(void)t;(void)c;return HAL_OK;}
HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *t,TIM_OC_InitTypeDef *c,uint32_t ch)
{assert(ch==TIM_CHANNEL_1&&c->OCPolarity==TIM_OCPOLARITY_HIGH);t->Instance->CCMR1=c->OCMode|8U;t->Instance->CCR1=c->Pulse;return HAL_OK;}
HAL_StatusTypeDef HAL_TIM_IC_ConfigChannel(TIM_HandleTypeDef *t,TIM_IC_InitTypeDef *c,uint32_t ch)
{assert(ch==TIM_CHANNEL_2);capture=*c;t->Instance->CCMR1|=0x100U|(c->ICFilter<<12);return HAL_OK;}
HAL_StatusTypeDef HAL_TIM_SlaveConfigSynchro(TIM_HandleTypeDef *t,TIM_SlaveConfigTypeDef *c)
{slave=*c;t->Instance->SMCR=c->SlaveMode|c->InputTrigger;return HAL_OK;}
HAL_StatusTypeDef HAL_TIMEx_ConfigBreakDeadTime(TIM_HandleTypeDef *t,TIM_BreakDeadTimeConfigTypeDef *c) {(void)t;(void)c;return HAL_OK;}
HAL_StatusTypeDef HAL_TIM_IC_Start(TIM_HandleTypeDef *t,uint32_t ch)
{assert(ch==TIM_CHANNEL_2);t->Instance->CCER|=0x10U;t->Instance->CR1|=1;return HAL_OK;}
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *t,uint32_t ch)
{assert(ch==TIM_CHANNEL_1);t->Instance->CCER|=1U;t->Instance->BDTR|=0x8000U;t->Instance->CR1|=1;return HAL_OK;}
int main(void)
{
    MX_TIM1_Init();
    assert(pins==(fan_ehtragh_Pin|zc_Pin));
    assert(TIM1->PSC==1919&&TIM1->ARR==999&&TIM1->CCR1==1000&&TIM1->DIER==0);
    assert((uint64_t)(TIM1->PSC+1U)*(TIM1->ARR+1U)*1000000U/64000000U==30000U);
    assert((TIM1->CCMR1&0x78U)==0x78U&&(TIM1->CCER&0x11U)==0x11U);
    assert(!(TIM1->CCER&4U)); /* complementary PB13 output must remain disabled */
    assert((TIM1->BDTR&0x8000U)&&(TIM1->CR1&1U));
    assert(slave.SlaveMode==TIM_SLAVEMODE_RESET&&slave.InputTrigger==TIM_TS_TI2FP2);
    assert(slave.TriggerPolarity==TIM_TRIGGERPOLARITY_RISING&&slave.TriggerFilter==3U);
    assert(capture.ICSelection==TIM_ICSELECTION_DIRECTTI&&capture.ICFilter==3U);
    for(unsigned compare=0;compare<=1000;++compare) {
        assert(TIM1_PhaseSetCompare((uint16_t)compare)==HAL_OK);
        unsigned low=0,high=0;
        for(unsigned cnt=0;cnt<1000;++cnt){if(cnt<TIM1->CCR1)++low;else ++high;}
        assert(low==compare&&high==1000U-compare);
    }
    assert(TIM1_PhaseSetCompare(1001)==HAL_ERROR&&TIM1->CCR1==1000);
    assert(TIM1_PhaseAdjustCompare(100)==HAL_OK&&TIM1->CCR1==1000);
    for(unsigned i=1;i<=10;++i)assert(TIM1_PhaseAdjustCompare(-100)==HAL_OK&&TIM1->CCR1==1000U-i*100U);
    assert(TIM1_PhaseAdjustCompare(-100)==HAL_OK&&TIM1->CCR1==0);
    for(unsigned i=1;i<=10;++i)assert(TIM1_PhaseAdjustCompare(100)==HAL_OK&&TIM1->CCR1==i*100U);
    assert(TIM1_PhaseAdjustCompare(-32768)==HAL_OK&&TIM1->CCR1==0);
    assert(TIM1_PhaseAdjustCompare(32767)==HAL_OK&&TIM1->CCR1==1000);
    puts("PASS TIM1 phase configuration: 1000 ticks/30ms, PA9 rising TI2 reset, PA8 PWM2, safe initial LOW, no IRQ/CH1N and compare bounds");
}
