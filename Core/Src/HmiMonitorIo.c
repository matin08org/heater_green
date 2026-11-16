#if defined(FW_CUSTOM_HMI)
#include "HmiMonitorIo.h"
#include "adc.h"
#include "usart.h"
#include "HmiDiagData.h"
#include <string.h>
static DMA_HandleTypeDef adc_dma,uart_dma;
static TIM_HandleTypeDef adc_timer;
static uint16_t adc_buffer[MONITOR_ADC_CHANNELS*2] __attribute__((aligned(4)));
static uint8_t uart_buffer[128] __attribute__((aligned(4)));
static HmiMonitorIoSnapshot published;
static uint16_t previous_rx;
static uint32_t retry_ms;
#if defined(FW_FINAL_APPLICATION)
static uint8_t rx_queue[256];
static volatile uint16_t rx_head,rx_tail;
#endif
static void AdcPublish(unsigned offset)
{
    for(unsigned i=0;i<MONITOR_ADC_CHANNELS;++i)published.raw[i]=adc_buffer[offset+i];
    published.adc_ms=HAL_GetTick();++published.adc_sequence;
}
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *adc) {if(adc==&hadc1)AdcPublish(0);}
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *adc) {if(adc==&hadc1)AdcPublish(MONITOR_ADC_CHANNELS);}
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *adc) {if(adc==&hadc1)++published.adc_errors;}
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *uart,uint16_t position)
{
    if(uart!=&huart1 || position>sizeof(uart_buffer))return;
    size_t keep;
    size_t length=HmiDiag_RxTail(published.rx,sizeof(published.rx),uart_buffer,sizeof(uart_buffer),previous_rx,position,&keep);
    if(!length)return;
#if defined(FW_FINAL_APPLICATION)
    unsigned start=previous_rx%sizeof(uart_buffer);
    for(unsigned i=0;i<length;++i) {
        uint16_t next=(uint16_t)((rx_head+1U)%sizeof(rx_queue));
        if(next==rx_tail){++published.rx_errors;break;}
        rx_queue[rx_head]=uart_buffer[(start+i)%sizeof(uart_buffer)];rx_head=next;
    }
#endif
    published.rx_length=(uint16_t)keep;published.rx_ms=HAL_GetTick();
    published.rx_bytes+=length;++published.rx_chunks;previous_rx=position;
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart) {if(uart==&huart1)++published.rx_errors;}
void HmiMonitorIo_Init(void)
{
    /* Dedicated resources, absent from other Core users: TIM5_CC1,
     * DMA2 Stream0/channel0 ADC1; Stream5/channel4 USART1_RX. SPI1 uses Stream2.
     * Acquisition runs even when DIAG is closed; the screen is a passive reader. */
    __HAL_RCC_DMA2_CLK_ENABLE();__HAL_RCC_TIM5_CLK_ENABLE();
    adc_dma.Instance=DMA2_Stream0;adc_dma.Init.Channel=DMA_CHANNEL_0;
    adc_dma.Init.Direction=DMA_PERIPH_TO_MEMORY;adc_dma.Init.PeriphInc=DMA_PINC_DISABLE;
    adc_dma.Init.MemInc=DMA_MINC_ENABLE;adc_dma.Init.PeriphDataAlignment=DMA_PDATAALIGN_HALFWORD;
    adc_dma.Init.MemDataAlignment=DMA_MDATAALIGN_HALFWORD;adc_dma.Init.Mode=DMA_CIRCULAR;
    adc_dma.Init.Priority=DMA_PRIORITY_HIGH;adc_dma.Init.FIFOMode=DMA_FIFOMODE_DISABLE;
    if(HAL_DMA_Init(&adc_dma)==HAL_OK) {
        __HAL_LINKDMA(&hadc1,DMA_Handle,adc_dma);
        hadc1.Init.ClockPrescaler=ADC_CLOCK_SYNC_PCLK_DIV4;
        hadc1.Init.ScanConvMode=ENABLE;hadc1.Init.ContinuousConvMode=DISABLE;
        hadc1.Init.ExternalTrigConvEdge=ADC_EXTERNALTRIGCONVEDGE_RISING;
        hadc1.Init.ExternalTrigConv=ADC_EXTERNALTRIGCONV_T5_CC1;
        hadc1.Init.NbrOfConversion=MONITOR_ADC_CHANNELS;hadc1.Init.DMAContinuousRequests=ENABLE;
        hadc1.Init.EOCSelection=ADC_EOC_SEQ_CONV;
        bool ok=HAL_ADC_Init(&hadc1)==HAL_OK;
        const uint32_t channels[]={ADC_CHANNEL_2,ADC_CHANNEL_3,ADC_CHANNEL_4,ADC_CHANNEL_5,ADC_CHANNEL_VREFINT,ADC_CHANNEL_TEMPSENSOR};
        for(unsigned i=0;i<MONITOR_ADC_CHANNELS && ok;++i) {
            ADC_ChannelConfTypeDef c={0};c.Channel=channels[i];c.Rank=i+1;
            c.SamplingTime=ADC_SAMPLETIME_480CYCLES;ok=HAL_ADC_ConfigChannel(&hadc1,&c)==HAL_OK;
        }
        adc_timer.Instance=TIM5;adc_timer.Init.Prescaler=63;adc_timer.Init.Period=49999;
        adc_timer.Init.CounterMode=TIM_COUNTERMODE_UP;
        /* ADC TIM5_CC1 needs an internally toggling channel, not the CC1IF
         * flag of Frozen/Timing mode. 1-ms HIGH per 50-ms period -> 20Hz.
         * Do NOT call GPIO post-init: PA0 remains the HC595 latch output. */
        TIM_OC_InitTypeDef oc={0};oc.OCMode=TIM_OCMODE_PWM1;oc.Pulse=1000;oc.OCPolarity=TIM_OCPOLARITY_HIGH;
        if(ok && HAL_TIM_PWM_Init(&adc_timer)==HAL_OK && HAL_TIM_PWM_ConfigChannel(&adc_timer,&oc,TIM_CHANNEL_1)==HAL_OK) {
            HAL_NVIC_SetPriority(DMA2_Stream0_IRQn,3,0);HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
            HAL_NVIC_SetPriority(ADC_IRQn,3,0);HAL_NVIC_EnableIRQ(ADC_IRQn);
            if(HAL_ADC_Start_DMA(&hadc1,(uint32_t *)(void *)adc_buffer,MONITOR_ADC_CHANNELS*2)==HAL_OK &&
                HAL_TIM_PWM_Start(&adc_timer,TIM_CHANNEL_1)==HAL_OK)published.adc_started=true;
        }
    }
    uart_dma.Instance=DMA2_Stream5;uart_dma.Init.Channel=DMA_CHANNEL_4;
    uart_dma.Init.Direction=DMA_PERIPH_TO_MEMORY;uart_dma.Init.PeriphInc=DMA_PINC_DISABLE;
    uart_dma.Init.MemInc=DMA_MINC_ENABLE;uart_dma.Init.PeriphDataAlignment=DMA_PDATAALIGN_BYTE;
    uart_dma.Init.MemDataAlignment=DMA_MDATAALIGN_BYTE;uart_dma.Init.Mode=DMA_CIRCULAR;
    uart_dma.Init.Priority=DMA_PRIORITY_HIGH;uart_dma.Init.FIFOMode=DMA_FIFOMODE_DISABLE;
    if(HAL_DMA_Init(&uart_dma)==HAL_OK) {
        __HAL_LINKDMA(&huart1,hdmarx,uart_dma);
        HAL_NVIC_SetPriority(DMA2_Stream5_IRQn,2,0);HAL_NVIC_EnableIRQ(DMA2_Stream5_IRQn);
        published.uart_started=HAL_UARTEx_ReceiveToIdle_DMA(&huart1,uart_buffer,sizeof(uart_buffer))==HAL_OK;
    }
}
void HmiMonitorIo_Process(void)
{
    if(HAL_GetTick()-retry_ms<100U)return;
    retry_ms=HAL_GetTick();
    if(huart1.hdmarx && huart1.RxState==HAL_UART_STATE_READY) {
        previous_rx=0;
        published.uart_started=HAL_UARTEx_ReceiveToIdle_DMA(&huart1,uart_buffer,sizeof(uart_buffer))==HAL_OK;
    }
}
void HmiMonitorIo_Get(HmiMonitorIoSnapshot *out)
{
    if(!out)return;
    uint32_t mask=__get_PRIMASK();__disable_irq();*out=published;
    out->adc_started=published.adc_started && (adc_dma.Instance->CR&DMA_SxCR_EN) && (TIM5->CR1&TIM_CR1_CEN);
    out->uart_started=published.uart_started && huart1.RxState==HAL_UART_STATE_BUSY_RX && (uart_dma.Instance->CR&DMA_SxCR_EN);
    __set_PRIMASK(mask);
}
void HmiMonitorIo_AdcDmaIRQ(void) {HAL_DMA_IRQHandler(&adc_dma);}
void HmiMonitorIo_UartDmaIRQ(void) {HAL_DMA_IRQHandler(&uart_dma);}
size_t HmiMonitorIo_ReadRx(uint8_t *dst,size_t capacity)
{
    if(!dst)return 0;
    size_t count=0;
#if defined(FW_FINAL_APPLICATION)
    uint32_t mask=__get_PRIMASK();__disable_irq();
    while(count<capacity&&rx_tail!=rx_head){dst[count++]=rx_queue[rx_tail];rx_tail=(uint16_t)((rx_tail+1U)%sizeof(rx_queue));}
    __set_PRIMASK(mask);
#else
    (void)capacity;
#endif
    return count;
}
#endif
