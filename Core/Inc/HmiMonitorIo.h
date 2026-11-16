#ifndef HMI_MONITOR_IO_H
#define HMI_MONITOR_IO_H
#include "main.h"
#include <stdbool.h>
#include <stddef.h>
#define MONITOR_ADC_CHANNELS 6U
typedef struct {
    uint16_t raw[MONITOR_ADC_CHANNELS]; /* PA2, PA3, PA4, PA5, VREFINT, CPU temp */
    uint32_t adc_ms,adc_sequence,adc_errors;
    uint32_t rx_ms,rx_chunks,rx_bytes,rx_errors;
    uint16_t rx_length;
    uint8_t rx[64];
    bool adc_started,uart_started;
} HmiMonitorIoSnapshot;
void HmiMonitorIo_Init(void);
void HmiMonitorIo_Process(void);
void HmiMonitorIo_Get(HmiMonitorIoSnapshot *out);
void HmiMonitorIo_AdcDmaIRQ(void);
void HmiMonitorIo_UartDmaIRQ(void);
size_t HmiMonitorIo_ReadRx(uint8_t *dst,size_t capacity);
#endif
