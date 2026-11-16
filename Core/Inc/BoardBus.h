#ifndef BOARD_BUS_H
#define BOARD_BUS_H
#include "main.h"
#include <stdbool.h>
typedef struct {
    bool enabled,debug_enabled;
    uint8_t plc_id,lux_id,debug_id;
    uint16_t plc_command_register,plc_status_register,lux_register;
    uint32_t baud,poll_ms,timeout_ms,quiet_ms,lux_stale_ms,lux_day_on_centi,lux_day_off_centi;
    uint8_t retries;
    char de_port;uint8_t de_pin;
    bool direction_confirmed;
    uint8_t parity,stop_bits;
} BoardBusConfig;
typedef struct {
    bool configured,lux_valid,daylight,plc_valid;
    uint32_t lux_centi,lux_ms,plc_ms,tx_count,completed,timeouts,crc_errors,protocol_errors,retries;
    uint8_t active_slot,last_exception;
} BoardBusSnapshot;
extern const BoardBusConfig board_bus_config;
void BoardBus_Init(void);
void BoardBus_Process(void);
void BoardBus_Get(BoardBusSnapshot *out);
void BoardBus_TxCompleteIRQ(void);
void BoardBus_TxDmaIRQ(void);
#endif
