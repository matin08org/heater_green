#if defined(FW_CUSTOM_HMI)
#include "main.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "DamperStepper.h"
#include "HmiMonitor.h"
#include "HmiMonitorIo.h"
#include "adc.h"
#include "Buzzer.h"
#if defined(FW_FINAL_APPLICATION)
#include "BoardBus.h"
#include "HmiAssetDma.h"
void DMA2_Stream1_IRQHandler(void) {HmiAssetDma_IRQHandler();}
void DMA2_Stream7_IRQHandler(void) {BoardBus_TxDmaIRQ();}
#endif
extern DMA_HandleTypeDef hdma_spi1_tx,hdma_spi2_rx,hdma_spi2_tx;
void SysTick_Handler(void) {HAL_IncTick();HmiMonitor_KeyTick();}
void DMA1_Stream3_IRQHandler(void) {HAL_DMA_IRQHandler(&hdma_spi2_rx);}
void DMA1_Stream4_IRQHandler(void) {HAL_DMA_IRQHandler(&hdma_spi2_tx);}
void DMA2_Stream2_IRQHandler(void) {HAL_DMA_IRQHandler(&hdma_spi1_tx);}
void DMA2_Stream0_IRQHandler(void) {HmiMonitorIo_AdcDmaIRQ();}
void DMA2_Stream5_IRQHandler(void) {HmiMonitorIo_UartDmaIRQ();}
void ADC_IRQHandler(void) {HAL_ADC_IRQHandler(&hadc1);}
void SPI1_IRQHandler(void) {HAL_SPI_IRQHandler(&hspi1);}
void SPI2_IRQHandler(void) {HAL_SPI_IRQHandler(&hspi2);}
void USART1_IRQHandler(void) {HAL_UART_IRQHandler(&huart1);}
void TIM2_IRQHandler(void) {DamperStepper_EndpointIRQHandler();}
void TIM6_IRQHandler(void) {HAL_TIM_IRQHandler(&htim6);}
void TIM4_IRQHandler(void) {Buzzer_TimerIRQ();}
#endif
