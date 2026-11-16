#include "MAX6675.h"
#include "HC595_SensorBus.h"
#include <stddef.h>

typedef enum { STARTUP, WAIT_CONVERSION, IN_TRANSACTION } SensorState;
static MAX6675_Data_t data[2] = {
    {.last_status=MAX6675_NOT_INITIALIZED},
    {.last_status=MAX6675_NOT_INITIALIZED}
};
static SensorState state;
static MAX6675_Sensor_t next_sensor;
static uint32_t conversion_ms, retry_ms;
static uint8_t initialized, retry_pending, data_initialized;
static uint32_t Lock(void) { uint32_t p=__get_PRIMASK(); __disable_irq(); return p; }
static void Unlock(uint32_t p) { __set_PRIMASK(p); }

static MAX6675_Status_t CheckSPI(void)
{
    uint32_t br=hspi2.Init.BaudRatePrescaler;
    if (br & ~SPI_CR1_BR) return MAX6675_SPI_CONFIG_ERROR;
    /* BR is encoded in CR1 bits 5:3 on STM32F4. */
    uint32_t divider=2U << (br>>3U);
    if (hspi2.Instance!=SPI2 || br>SPI_BAUDRATEPRESCALER_256 ||
        hspi2.Init.Mode!=SPI_MODE_MASTER || hspi2.Init.Direction!=SPI_DIRECTION_2LINES ||
        hspi2.Init.DataSize!=SPI_DATASIZE_8BIT || hspi2.Init.FirstBit!=SPI_FIRSTBIT_MSB ||
        hspi2.Init.CLKPolarity!=SPI_POLARITY_LOW || hspi2.Init.CLKPhase!=SPI_PHASE_1EDGE ||
        HAL_RCC_GetPCLK1Freq()/divider>MAX6675_MAX_SPI_CLOCK_HZ)
        return MAX6675_SPI_CONFIG_ERROR;
    return MAX6675_OK;
}

MAX6675_Status_t MAX6675_Init(void)
{
    uint32_t p;
    if (initialized) return MAX6675_BUSY;
    if (CheckSPI()!=MAX6675_OK) return MAX6675_SPI_CONFIG_ERROR;
    if (HC595_SensorBegin(1U,0U,MAX6675_CS_SETTLE_US)!=HAL_OK) return MAX6675_BUSY;
    p=Lock();
    if (!data_initialized) {
        for (uint32_t i=0;i<2U;i++) data[i]=(MAX6675_Data_t){.last_status=MAX6675_NO_NEW_DATA};
        data_initialized=1U;
    }
    initialized=1U;
    Unlock(p);
    next_sensor=MAX6675_SENSOR_1;
    state=STARTUP;
    retry_pending=0U;
    return MAX6675_OK;
}

static MAX6675_Status_t StoreAttempt(MAX6675_Sensor_t sensor,const HC595_SensorResult_t *r)
{
    MAX6675_Status_t status=(r->status!=HAL_OK)?MAX6675_SPI_ERROR:
                             ((r->raw&4U)?MAX6675_OPEN_THERMOCOUPLE:MAX6675_OK);
    uint32_t p=Lock();
    MAX6675_Data_t *d=&data[sensor];
    d->last_attempt_ms=HAL_GetTick();
    d->last_status=status;
    if (status==MAX6675_SPI_ERROR) d->spi_error_count++;
    if (status==MAX6675_OPEN_THERMOCOUPLE) d->open_error_count++;
    d->thermocouple_open=(status==MAX6675_OPEN_THERMOCOUPLE);
    if (r->status==HAL_OK) d->raw=r->raw;
    if (status==MAX6675_OK) {
        d->temperature_q4=(r->raw>>3U)&0x0FFFU;
        d->temperature_c=d->temperature_q4*0.25f;
        d->has_valid_sample=1U;
        d->last_valid_ms=d->timestamp_ms=d->last_attempt_ms;
    }
    Unlock(p);
    return status;
}

MAX6675_Status_t MAX6675_Process(MAX6675_Sensor_t *updated)
{
    HC595_SensorResult_t r;
    HAL_StatusTypeDef s;
    uint32_t now=HAL_GetTick();
    if (updated) *updated=MAX6675_SENSOR_NONE;
    HC595_Process();
    if (!initialized) return MAX6675_NOT_INITIALIZED;
    if (state==WAIT_CONVERSION) {
        /* One extra tick avoids selecting up to 1 ms early at tick boundaries. */
        if ((uint32_t)(now-conversion_ms)<=MAX6675_CONVERSION_TIME_MS ||
            (retry_pending && (uint32_t)(now-retry_ms)<MAX6675_CONVERSION_TIME_MS))
            return MAX6675_NO_NEW_DATA;
        s=HC595_SensorBegin(next_sensor==MAX6675_SENSOR_2,1U,MAX6675_CS_SETTLE_US);
        if (s!=HAL_OK) return MAX6675_BUSY;
        state=IN_TRANSACTION;
        return MAX6675_BUSY;
    }
    if (HC595_SensorPoll(&r)==HAL_BUSY) return MAX6675_BUSY;
    if (state==STARTUP) {
        if (r.status==HAL_OK && r.latched) {
            conversion_ms=r.latch_ms;
            state=WAIT_CONVERSION;
            return MAX6675_NO_NEW_DATA;
        }
        /* Startup fault is observable for both sensors; still no valid sample. */
        r.status=HAL_ERROR;
        StoreAttempt(MAX6675_SENSOR_1,&r);
        StoreAttempt(MAX6675_SENSOR_2,&r);
        initialized=0U; /* Explicitly retry Init from App_Process. */
        return MAX6675_SPI_ERROR;
    }
    MAX6675_Status_t status=StoreAttempt(next_sensor,&r);
    if (updated) *updated=next_sensor; /* Includes failed attempts. */
    if (r.latched) {
        conversion_ms=r.latch_ms; /* Other sensor began conversion at this edge. */
        next_sensor=(next_sensor==MAX6675_SENSOR_1)?MAX6675_SENSOR_2:MAX6675_SENSOR_1;
        retry_pending=0U;
    } else { retry_ms=now; retry_pending=1U; }
    state=WAIT_CONVERSION;
    return status;
}

void MAX6675_GetSnapshot(MAX6675_Snapshot_t *out)
{
    uint32_t p;
    if (!out) return;
    p=Lock();
    out->sensor[0]=data[0];
    out->sensor[1]=data[1];
    out->captured_ms=HAL_GetTick();
    Unlock(p);
    for (uint32_t i=0;i<2U;i++) {
        MAX6675_Data_t *d=&out->sensor[i];
        d->stale=!d->has_valid_sample || (uint32_t)(out->captured_ms-d->last_valid_ms)>MAX6675_STALE_TIME_MS;
        d->valid=d->has_valid_sample && !d->stale && d->last_status==MAX6675_OK;
    }
}
MAX6675_Status_t MAX6675_GetData(MAX6675_Sensor_t sensor,MAX6675_Data_t *out)
{
    MAX6675_Snapshot_t snapshot;
    if ((uint32_t)sensor>=2U || !out) return MAX6675_INVALID_ARGUMENT;
    MAX6675_GetSnapshot(&snapshot);
    *out=snapshot.sensor[sensor];
    return out->last_status;
}
MAX6675_Status_t MAX6675_GetTemperature(MAX6675_Sensor_t sensor,float *temperature)
{
    MAX6675_Data_t d;
    MAX6675_Status_t s;
    if (!temperature) return MAX6675_INVALID_ARGUMENT;
    s=MAX6675_GetData(sensor,&d);
    if ((uint32_t)sensor>=2U) return s;
    if (!d.valid) return (s==MAX6675_OK)?MAX6675_NO_NEW_DATA:s;
    *temperature=d.temperature_c;
    return s;
}
uint8_t MAX6675_IsDataValid(MAX6675_Sensor_t sensor)
{
    MAX6675_Data_t d;
    if ((uint32_t)sensor>=2U) return 0U;
    (void)MAX6675_GetData(sensor,&d);
    return d.valid;
}
