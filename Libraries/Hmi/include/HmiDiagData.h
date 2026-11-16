#ifndef HMI_DIAG_DATA_H
#define HMI_DIAG_DATA_H
#include "HmiTypes.h"
typedef struct {
    bool voltage_valid,cpu_temperature_valid;
    uint32_t vdda_mv;
    uint16_t millivolts[6];
    int32_t differential_raw,differential_mv,cpu_temperature_tenths;
} HmiDiagAnalog;
/* Converts only calibrated electrical quantities, not uncalibrated ME2 ppm
 * or NTC degrees. Factory cal values refer to VDDA=3300mV, 30C/110C. */
void HmiDiag_Analog(const uint16_t raw[6],bool fresh,uint16_t vref_cal,
                    uint16_t temp_cal_30,uint16_t temp_cal_110,HmiDiagAnalog *out);
/* Last bytes from a circular DMA chunk. Position and previous may equal size
 * (HAL transfer-complete convention). Returns total new bytes; retained is
 * the number copied, never more than capacity. Caller synchronizes DMA/IRQ. */
size_t HmiDiag_RxTail(uint8_t *dst,size_t capacity,const uint8_t *ring,
                       size_t size,size_t previous,size_t position,size_t *retained);
#endif
