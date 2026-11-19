#ifndef BURNER_APP_H
#define BURNER_APP_H
#include "BurnerControl.h"
#include "MAX6675.h"
#include "co2_link.h"
typedef struct {
    BurnerState state;
    BurnerFault fault;
    BurnerSource command_source, fault_source;
    BurnerMode mode;
    BurnerOutputs requested;
    BurnerCommandResult last_start_result;
    uint32_t fault_sequence;
    uint32_t start_blockers; /* Reasons a new start cannot be accepted. */
    uint16_t co2_target_ppm;
    bool configured, actuators_inhibited;
    bool simulated; /* DRY RUN model state; never evidence of physical operation */
    bool output_test; /* Electrical dummy-load test, never combustion operation */
    BurnerSampleState sample_state;
    uint8_t air_percent,air_adjustments;
    bool bypass_proven,process_proven,stopped_proven;
    BurnerTiming timing;
    uint16_t co_accept_ppm,co_limit_ppm,co_trip_ppm;
} BurnerAppSnapshot;
/* Call after App_Init + ui_init. All commands/process calls have one main
 * context owner. IRQ/other task consumers may copy the published snapshot.
 * PLC integration must queue commands to main, not call from a UART ISR.
 * This board adapter is deliberately output-inhibited until commissioning.
 */
void BurnerApp_Init(void);
void BurnerApp_Process(const MAX6675_Snapshot_t *temperatures, const CO2_Snapshot *co2);
/* No LVGL: main-context control/acquisition service, also during DMA waits.
 * Process above now renders cached state only, outside LVGL callbacks. */
void BurnerApp_ServiceInputs(const MAX6675_Snapshot_t *temperatures,const CO2_Snapshot *co2);
BurnerCommandResult BurnerApp_Start(BurnerSource source);
void BurnerApp_Stop(BurnerSource source);
bool BurnerApp_AcknowledgeFault(void); /* Explicit local user action only. */
bool BurnerApp_SetMode(BurnerMode mode);
bool BurnerApp_SetCO2Target(uint16_t ppm);
void BurnerApp_GetSnapshot(BurnerAppSnapshot *snapshot);
#endif
