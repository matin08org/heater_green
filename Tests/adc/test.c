#include "HmiMonitorIo.h"
#include "HmiDiagData.h"
#include "adc.h"
#include "usart.h"
#include <assert.h>
#include <stdio.h>
DmaRegs dma0,dma5;TimerRegs timer5;
ADC_HandleTypeDef hadc1;
UART_HandleTypeDef huart1;
static uint32_t tick,primask,mode,rank_count,dma_irqs;
static uint16_t *buffer;static unsigned half;
static TIM_HandleTypeDef *trigger_timer;
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef*);
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef*);
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef*);
uint32_t __get_PRIMASK(void) {return primask;}
void __disable_irq(void) {primask=1;}
void __set_PRIMASK(uint32_t value) {primask=value;}
uint32_t HAL_GetTick(void) {return tick;}
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *d)
{assert(d->Init.Mode==DMA_CIRCULAR&&d->Init.Direction==DMA_PERIPH_TO_MEMORY);return HAL_OK;}
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *d) {assert(d==hadc1.DMA_Handle||d==huart1.hdmarx);++dma_irqs;}
HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *a)
{
    assert(a->Init.ScanConvMode&&a->Init.NbrOfConversion==6&&!a->Init.ContinuousConvMode);
    assert(a->Init.ClockPrescaler==ADC_CLOCK_SYNC_PCLK_DIV4&&a->Init.DMAContinuousRequests);
    assert(a->Init.ExternalTrigConv==ADC_EXTERNALTRIGCONV_T5_CC1&&a->Init.ExternalTrigConvEdge==ADC_EXTERNALTRIGCONVEDGE_RISING);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *a,ADC_ChannelConfTypeDef *c)
{
    (void)a;const unsigned channels[]={2,3,4,5,17,16};
    assert(c->Rank==rank_count+1U&&c->Channel==channels[rank_count]&&c->SamplingTime==ADC_SAMPLETIME_480CYCLES);
    ++rank_count;return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *a,uint32_t *data,uint32_t length)
{
    assert(rank_count==6&&length==12&&a->DMA_Handle->Instance==DMA2_Stream0);
    assert(a->DMA_Handle->Init.Channel==0&&a->DMA_Handle->Init.MemInc);
    assert(a->DMA_Handle->Init.MemDataAlignment==DMA_MDATAALIGN_HALFWORD);
    buffer=(uint16_t *)(void *)data;dma0.CR=DMA_SxCR_EN;return HAL_OK;
}
HAL_StatusTypeDef HAL_TIM_PWM_Init(TIM_HandleTypeDef *t)
{assert(t->Instance==TIM5&&t->Init.Prescaler==63&&t->Init.Period==49999);trigger_timer=t;return HAL_OK;}
HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *t,TIM_OC_InitTypeDef *c,uint32_t channel)
{assert(t==trigger_timer&&channel==TIM_CHANNEL_1&&c->Pulse==1000&&c->OCMode==TIM_OCMODE_PWM1);mode=c->OCMode;return HAL_OK;}
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *t,uint32_t channel)
{assert(buffer&&channel==TIM_CHANNEL_1&&mode==TIM_OCMODE_PWM1);t->Instance->CR1|=TIM_CR1_CEN;return HAL_OK;}
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *u,uint8_t *data,uint16_t length)
{assert(data&&length==128&&u->hdmarx->Instance==DMA2_Stream5);u->RxState=HAL_UART_STATE_BUSY_RX;dma5.CR=DMA_SxCR_EN;return HAL_OK;}
void HAL_NVIC_SetPriority(int irq,unsigned priority,unsigned sub) {(void)irq;(void)priority;(void)sub;}
void HAL_NVIC_EnableIRQ(int irq) {(void)irq;}
static void Trigger(void)
{
    if(!(TIM5->CR1&TIM_CR1_CEN)||mode!=TIM_OCMODE_PWM1)return;
    const uint16_t sample[]={129,379,3159,655,1515,979};
    for(unsigned i=0;i<6;++i)buffer[half*6U+i]=sample[i];
    if(!half)HAL_ADC_ConvHalfCpltCallback(&hadc1);else HAL_ADC_ConvCpltCallback(&hadc1);
    half^=1U;
}
int main(void)
{
    HmiMonitorIoSnapshot s;HmiMonitorIo_Init();HmiMonitorIo_Get(&s);
    assert(s.adc_started&&s.uart_started&&!s.adc_sequence);
    /* A running counter in Frozen mode is not sufficient to acquire samples. */
    mode=0;tick=50;Trigger();HmiMonitorIo_Get(&s);assert(s.adc_sequence==0);
    mode=TIM_OCMODE_PWM1;
    for(unsigned n=1;n<=5;++n){tick+=50;Trigger();HmiMonitorIo_Get(&s);assert(s.adc_sequence==n&&s.adc_ms==tick&&s.raw[4]==1515);}
    HmiDiagAnalog analog;HmiDiag_Analog(s.raw,true,1506,900,1200,&analog);
    assert(analog.voltage_valid&&analog.vdda_mv==3280&&s.raw[0]==129&&s.raw[2]==3159);
    primask=1;HmiMonitorIo_Get(&s);assert(primask==1);primask=0;
    HmiMonitorIo_AdcDmaIRQ();assert(dma_irqs==1);
    HAL_ADC_ErrorCallback(&hadc1);HmiMonitorIo_Get(&s);assert(s.adc_errors==1);
    TIM5->CR1=0;HmiMonitorIo_Get(&s);assert(!s.adc_started);
    puts("PASS ADC acquisition: internal PWM trigger vs Frozen, six ranks, circular DMA halves, real VREF conversion, timestamps, IRQ and snapshot ownership");
}
