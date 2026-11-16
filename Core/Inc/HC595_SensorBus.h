#ifndef HC595_SENSOR_BUS_H
#define HC595_SENSOR_BUS_H
/* Private interface: MAX6675 is the only owner allowed to change Q0.
 * SPI2 transactions MUST go through HC595.c, never raw HAL calls elsewhere. */
#include "HC595.h"
typedef struct {
    HAL_StatusTypeDef status;
    uint8_t latched;
    uint16_t raw;
    uint32_t latch_ms;
} HC595_SensorResult_t;
HAL_StatusTypeDef HC595_SensorBegin(uint8_t q0, uint8_t read, uint32_t settle_us);
/* HAL_BUSY while active; copies the result and releases the bus on completion. */
HAL_StatusTypeDef HC595_SensorPoll(HC595_SensorResult_t *result);
#endif
