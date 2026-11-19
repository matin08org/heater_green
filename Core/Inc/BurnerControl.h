#ifndef BURNER_CONTROL_H
#define BURNER_CONTROL_H
#include <stdbool.h>
#include <stdint.h>

/* Supervisory sequencer, NOT a certified flame safeguard. One main-context
 * owner for all calls. Inputs are validated snapshots; output booleans are
 * REQUESTS only. Independent hardware must isolate fuel on faults/reset. */
typedef enum {
    BURNER_IDLE, BURNER_PRECHECK, BURNER_PURGE, BURNER_IGNITION_1,
    BURNER_INTERPURGE, BURNER_IGNITION_2, BURNER_RUNNING,
    BURNER_SHUTDOWN, BURNER_POSTPURGE, BURNER_LOCKOUT
} BurnerState;
typedef enum { BURNER_HMI, BURNER_PLC, BURNER_AUTO } BurnerSource;
typedef enum { BURNER_MANUAL_MODE, BURNER_AUTO_MODE } BurnerMode;
typedef enum {
    BURNER_NO_FAULT, BURNER_CONFIGURATION, BURNER_INTERLOCK,
    BURNER_DETECTOR, BURNER_FALSE_FLAME, BURNER_AIRFLOW,
    BURNER_IGNITION_FAILED, BURNER_FLAME_LOST, BURNER_INPUT_STALE,
    BURNER_CO_HIGH, BURNER_CO_TIMEOUT, BURNER_ADJUST_LIMIT,
    BURNER_COOLING_TIMEOUT, BURNER_TEMPERATURE, BURNER_ACTUATION
} BurnerFault;
typedef enum {
    SAMPLE_IDLE, SAMPLE_WAIT_BYPASS, SAMPLE_WAIT_TEMPERATURE,
    SAMPLE_VALVE_1, SAMPLE_COOLING, SAMPLE_VALVE_2,
    SAMPLE_WAIT_CO, SAMPLE_ACCEPTED, SAMPLE_AIR_SETTLE
} BurnerSampleState;
typedef enum { DAMPER_HOLD, DAMPER_BYPASS, DAMPER_PROCESS } BurnerDamper;
typedef enum { COOL_BY_TIME, COOL_BY_TEMPERATURE } BurnerCoolingMode;
typedef enum {
    BURNER_COMMAND_ACCEPTED, BURNER_COMMAND_BUSY,
    BURNER_COMMAND_CONFIG_REQUIRED, BURNER_COMMAND_INHIBITED
} BurnerCommandResult;

typedef struct {
    bool commissioned; /* Set only after design review + hardware validation. */
    uint32_t purge_ms, required_purge_ms; /* 30 s floor from user specification. */
    uint32_t interpurge_ms, postpurge_ms, ignition_ms, flame_stable_ms;
    uint32_t airflow_timeout_ms, input_max_age_ms;
    uint32_t co_min_wait_ms, co_timeout_ms;
    uint16_t co_limit_ppm;
    uint16_t co2_hysteresis_ppm; /* stop >= target; allow a new AUTO start <= target - hysteresis */
    uint8_t max_attempts; /* 1 or 2, selected by approved burner design. */
    bool fault_postpurge_permitted;
    BurnerCoolingMode cooling_mode;
    bool adaptive_air_enabled;
    uint32_t initial_measure_delay_ms,sample_period_ms,sample_fill_ms;
    uint32_t sample_transfer_ms,cooling_timeout_ms,air_settle_ms;
    uint32_t damper_timeout_ms;
    uint16_t co_trip_ppm,co_accept_ppm;
    uint8_t air_initial_percent,air_min_percent,air_max_percent,air_step_percent,max_adjustments;
} BurnerConfig;

typedef struct {
    uint32_t captured_ms;
    bool interlocks_ok, detector_healthy, flame;
    bool airflow_proof_valid, airflow, ventilation_safe;
    bool fuel_off_proven, ignition_off_proven, fan_stopped_proven;
    bool flame_temp_valid, cooler_temp_valid;
    int16_t flame_temp_q4, cooler_temp_q4;
    bool co_valid;
    uint16_t co_ppm;
    uint32_t co_sample_ms;
    bool co2_valid, daylight_valid, daylight;
    uint16_t co2_ppm;
    bool bypass_proven, process_proven;
} BurnerInputs;
typedef struct {
    bool gas, ignition, combustion_fan, sample_valve_1, sample_valve_2, cooler_fan;
    BurnerDamper damper;
    bool exchanger_fan,distribution_fan;
    uint8_t air_percent;
} BurnerOutputs;
typedef struct {
    BurnerConfig config;
    BurnerState state;
    BurnerSampleState sample_state;
    BurnerFault fault;
    BurnerSource cycle_source, last_command_source, fault_source;
    BurnerMode mode;
    BurnerOutputs out;
    uint32_t state_ms, sample_ms, flame_ms, purge_ms, fault_ms;
    uint32_t fault_sequence;
    uint16_t co2_target_ppm;
    uint8_t attempt;
    bool tracking_flame, tracking_purge, fault_purge_active;
    uint8_t air_adjustments;
    uint16_t previous_adjust_co;
    bool have_previous_adjust_co;
    uint32_t sample_cycle_ms;
    uint8_t sample_attempts;
} BurnerControl;
typedef struct {
    uint32_t state_elapsed_ms,state_remaining_ms,sample_remaining_ms;
    bool state_timer_active,sample_timer_active,purge_counting;
} BurnerTiming;
void Burner_GetTiming(const BurnerControl *b,uint32_t now,BurnerTiming *out);

bool Burner_ConfigValid(const BurnerConfig *c);
bool Burner_ParametersValid(const BurnerConfig *c);
/* Read-only, mode-aware preflight. Hardware status must describe actual
 * acquisition capability, never relay requests or operator assumptions. */
typedef struct {
    bool proof_inputs_connected, fan_calibrated, fan_sync;
    bool damper_referenced, actuation_fault, co_calibrated;
} BurnerHardwareStatus;
enum {
    BURNER_BLOCK_PROOF_CONNECTION = 1U << 0,
    BURNER_BLOCK_PARAMETERS = 1U << 1,
    BURNER_BLOCK_COMMISSIONING = 1U << 2,
    BURNER_BLOCK_FAN_CALIBRATION = 1U << 3,
    BURNER_BLOCK_CO_CALIBRATION = 1U << 4,
    BURNER_BLOCK_DAMPER = 1U << 5,
    BURNER_BLOCK_FAN_SYNC = 1U << 6,
    BURNER_BLOCK_ACTUATION = 1U << 7,
    BURNER_BLOCK_STALE = 1U << 8,
    BURNER_BLOCK_PROOF_STATE = 1U << 9,
    BURNER_BLOCK_TEMPERATURE_INPUT = 1U << 10,
    BURNER_BLOCK_CO2 = 1U << 11,
    BURNER_BLOCK_DAYLIGHT = 1U << 12,
    BURNER_BLOCK_BUSY = 1U << 13
};
uint32_t Burner_StartBlockers(const BurnerControl *b,const BurnerInputs *in,
                             const BurnerHardwareStatus *hardware,uint32_t now);
const char *Burner_StartBlockText(uint32_t blockers);
bool Burner_AutoStartRequested(const BurnerControl *b,const BurnerInputs *in,uint32_t now,bool armed);
/* Five-volume calculation is a sizing aid, not approval of ventilation. */
uint32_t Burner_FiveVolumePurgeMs(uint32_t volume_l, uint32_t verified_flow_l_min);
void Burner_Init(BurnerControl *b, const BurnerConfig *config, uint32_t now);
BurnerCommandResult Burner_RequestStart(BurnerControl *b, BurnerSource source,
                                       const BurnerInputs *in, uint32_t now);
void Burner_RequestStop(BurnerControl *b, BurnerSource source, uint32_t now);
/* Main-context report from the physical adapter; latches a fault during a cycle. */
void Burner_ReportActuationFault(BurnerControl *b,const BurnerInputs *in,uint32_t now);
bool Burner_AcknowledgeFault(BurnerControl *b, const BurnerInputs *in, uint32_t now);
void Burner_Process(BurnerControl *b, const BurnerInputs *in, uint32_t now);
const char *Burner_StateText(BurnerState state);
const char *Burner_FaultText(BurnerFault fault);
const char *Burner_SampleText(BurnerSampleState state);
#endif
