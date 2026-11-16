#ifndef SENSOR_MATH_H
#define SENSOR_MATH_H
#include "HmiDiagData.h"
typedef struct {bool calibrated;float pullup_ohms,r25_ohms,beta;} NtcCalibration;
typedef struct {
    bool calibrated;
    float zero_uv,sensitivity_uv_per_ppm,reference_c,span_temp_coefficient;
    NtcCalibration me2_ntc,mixer_ntc;
} BoardAnalogCalibration;
typedef struct {
    bool co_valid,me2_temperature_valid,mixer_temperature_valid;
    uint16_t co_ppm;
    int32_t me2_temperature_q4,mixer_temperature_q4;
} BoardAnalogValues;
void SensorMath_Convert(const uint16_t raw[6],const HmiDiagAnalog *electrical,
                        const BoardAnalogCalibration *cal,BoardAnalogValues *out);
extern const BoardAnalogCalibration board_analog_calibration;
#endif
