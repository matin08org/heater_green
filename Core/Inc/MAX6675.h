#ifndef MAX6675_H
#define MAX6675_H
#include "HC595.h"
#ifndef MAX6675_CONVERSION_TIME_MS
#define MAX6675_CONVERSION_TIME_MS 500U
#endif
#ifndef MAX6675_CS_SETTLE_US
#define MAX6675_CS_SETTLE_US 20U
#endif
#define MAX6675_STALE_TIME_MS 2000U
#define MAX6675_MAX_SPI_CLOCK_HZ 1000000UL
#if MAX6675_CONVERSION_TIME_MS < 500U
#error "MAX6675 requires at least 500 ms conversion margin in this design"
#endif

typedef enum { MAX6675_SENSOR_1=0, MAX6675_SENSOR_2=1, MAX6675_SENSOR_NONE=0xFF } MAX6675_Sensor_t;
typedef enum {
    MAX6675_OK=0, MAX6675_NO_NEW_DATA, MAX6675_BUSY, MAX6675_OPEN_THERMOCOUPLE,
    MAX6675_NOT_INITIALIZED, MAX6675_INVALID_ARGUMENT, MAX6675_SPI_ERROR,
    MAX6675_SPI_CONFIG_ERROR, MAX6675_HC595_ERROR
} MAX6675_Status_t;
typedef struct {
    uint16_t temperature_q4;  /* Last VALID value; one unit = 0.25 C. */
    uint16_t raw;             /* Raw data from last completed SPI read. */
    float temperature_c;     /* Compatibility view of last VALID value. */
    uint32_t timestamp_ms;    /* Compatibility alias of last_valid_ms. */
    uint32_t last_valid_ms;
    uint32_t last_attempt_ms;
    uint32_t spi_error_count;
    uint32_t open_error_count;
    MAX6675_Status_t last_status;
    uint8_t has_valid_sample;
    uint8_t stale;
    uint8_t valid;            /* Fresh valid value AND last attempt succeeded. */
    uint8_t thermocouple_open;
} MAX6675_Data_t;
typedef struct { MAX6675_Data_t sensor[2]; uint32_t captured_ms; } MAX6675_Snapshot_t;

/* Init schedules Q0=1; first valid sample cannot arrive until 500 ms AFTER
 * its physical latch. Process runs from one main/task owner, never an ISR.
 * SPI2 transfers use IRQs; no conversion/settling/transfer polling loops. */
MAX6675_Status_t MAX6675_Init(void);
MAX6675_Status_t MAX6675_Process(MAX6675_Sensor_t *updated_sensor);
/* Snapshot/getters copy cached data under an IRQ critical section. No SPI.
 * Snapshot returns both sensor states even before any valid sample. */
void MAX6675_GetSnapshot(MAX6675_Snapshot_t *snapshot);
MAX6675_Status_t MAX6675_GetData(MAX6675_Sensor_t sensor,MAX6675_Data_t *data);
/* Only returns a temperature when fresh and the last attempt succeeded. */
MAX6675_Status_t MAX6675_GetTemperature(MAX6675_Sensor_t sensor,float *temperature_c);
uint8_t MAX6675_IsDataValid(MAX6675_Sensor_t sensor);
#endif
