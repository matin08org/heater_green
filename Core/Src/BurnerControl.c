#include "BurnerControl.h"
#include "CombustionCycle.h"
#include <limits.h>
#include <string.h>

static bool Elapsed(uint32_t now, uint32_t then, uint32_t duration)
{ return (uint32_t)(now - then) >= duration; }
static bool Duration(uint32_t ms) { return ms > 0U && ms < INT32_MAX; }
bool Burner_ParametersValid(const BurnerConfig *c)
{
    return c && c->purge_ms >= 30000U &&
        Duration(c->required_purge_ms) && c->purge_ms >= c->required_purge_ms &&
        Duration(c->purge_ms) && Duration(c->interpurge_ms) &&
        Duration(c->postpurge_ms) && Duration(c->ignition_ms) &&
        c->ignition_ms <= 5000U && Duration(c->flame_stable_ms) &&
        c->flame_stable_ms < c->ignition_ms && Duration(c->airflow_timeout_ms) &&
        Duration(c->input_max_age_ms) && Duration(c->co_min_wait_ms) &&
        Duration(c->co_timeout_ms) && c->co_timeout_ms > c->co_min_wait_ms &&
        c->co_limit_ppm > 0U && c->co2_hysteresis_ppm>0U && c->co2_hysteresis_ppm<=5000U &&
        (c->max_attempts == 1U || c->max_attempts == 2U) &&
        (c->cooling_mode == COOL_BY_TIME || c->cooling_mode == COOL_BY_TEMPERATURE) &&
        (!c->adaptive_air_enabled || (c->cooling_mode==COOL_BY_TEMPERATURE &&
          Duration(c->initial_measure_delay_ms)&&Duration(c->sample_period_ms)&&Duration(c->sample_fill_ms)&&
          Duration(c->sample_transfer_ms)&&Duration(c->cooling_timeout_ms)&&Duration(c->air_settle_ms)&&
          Duration(c->damper_timeout_ms)&&
          c->co_accept_ppm<c->co_limit_ppm&&c->co_trip_ppm>c->co_limit_ppm&&
          c->air_min_percent>0U&&c->air_min_percent<c->air_max_percent&&c->air_max_percent<=100U&&
          c->air_initial_percent>=c->air_min_percent&&c->air_initial_percent<=c->air_max_percent&&
          c->air_step_percent>0U&&c->air_step_percent<=10U&&c->max_adjustments>0U&&c->max_adjustments<=10U));
}
bool Burner_ConfigValid(const BurnerConfig *c)
{return c && c->commissioned && Burner_ParametersValid(c);}
uint32_t Burner_StartBlockers(const BurnerControl *b,const BurnerInputs *in,
                             const BurnerHardwareStatus *h,uint32_t now)
{
    if(!b||!in||!h)return BURNER_BLOCK_STALE|BURNER_BLOCK_PROOF_CONNECTION;
    uint32_t mask=0;
    if(!h->proof_inputs_connected)mask|=BURNER_BLOCK_PROOF_CONNECTION;
    if(!Burner_ParametersValid(&b->config))mask|=BURNER_BLOCK_PARAMETERS;
    if(!b->config.commissioned)mask|=BURNER_BLOCK_COMMISSIONING;
    if(!h->fan_calibrated)mask|=BURNER_BLOCK_FAN_CALIBRATION;
    if(!h->co_calibrated)mask|=BURNER_BLOCK_CO_CALIBRATION;
    if(!h->damper_referenced||!in->bypass_proven)mask|=BURNER_BLOCK_DAMPER;
    if(!h->fan_sync)mask|=BURNER_BLOCK_FAN_SYNC;
    if(h->actuation_fault)mask|=BURNER_BLOCK_ACTUATION;
    if(!b->config.input_max_age_ms||Elapsed(now,in->captured_ms,b->config.input_max_age_ms))mask|=BURNER_BLOCK_STALE;
    if(!in->interlocks_ok||!in->detector_healthy||!in->airflow_proof_valid||
       !in->ventilation_safe||in->airflow||in->flame||!in->fuel_off_proven||
       !in->ignition_off_proven||!in->fan_stopped_proven)mask|=BURNER_BLOCK_PROOF_STATE;
    if(!in->flame_temp_valid||!in->cooler_temp_valid)mask|=BURNER_BLOCK_TEMPERATURE_INPUT;
    if(b->mode==BURNER_AUTO_MODE) {
        uint16_t limit=b->co2_target_ppm>b->config.co2_hysteresis_ppm?
            b->co2_target_ppm-b->config.co2_hysteresis_ppm:0;
        if(!b->co2_target_ppm||!in->co2_valid||in->co2_ppm>limit)mask|=BURNER_BLOCK_CO2;
        if(!in->daylight_valid||!in->daylight)mask|=BURNER_BLOCK_DAYLIGHT;
    }
    if(b->state!=BURNER_IDLE||b->fault!=BURNER_NO_FAULT)mask|=BURNER_BLOCK_BUSY;
    return mask;
}
const char *Burner_StartBlockText(uint32_t mask)
{
    static const char *const text[]={"SAFETY INPUTS NOT WIRED","PROCESS SETTINGS MISSING",
        "COMMISSIONING REQUIRED","FAN CALIBRATION REQUIRED","CO CALIBRATION REQUIRED",
        "EXHAUST REFERENCE REQUIRED","FAN SYNC NOT READY","ACTUATOR FAULT",
        "INPUTS STALE","START CONDITIONS NOT MET","TEMPERATURE INPUT INVALID",
        "CO2 DEMAND NOT READY","DAYLIGHT NOT READY","PROCESS BUSY / LOCKOUT"};
    for(unsigned i=0;i<sizeof(text)/sizeof(text[0]);++i)if(mask&(1UL<<i))return text[i];
    return mask?"UNKNOWN START BLOCK":"READY";
}
uint32_t Burner_FiveVolumePurgeMs(uint32_t volume_l, uint32_t flow)
{
    if (!volume_l || !flow) return UINT32_MAX;
    uint64_t ms = ((uint64_t)volume_l * 300000U + flow - 1U) / flow;
    return ms > UINT32_MAX ? UINT32_MAX : (uint32_t)ms;
}
static void CancelSampling(BurnerControl *b)
{
    b->sample_state = SAMPLE_IDLE;
    b->out.sample_valve_1 = b->out.sample_valve_2 = b->out.cooler_fan = false;
    b->out.damper = DAMPER_HOLD;
    b->out.distribution_fan=false;
}
static void FuelOff(BurnerControl *b)
{
    b->out.gas = b->out.ignition = false;
    CancelSampling(b);
    if(b->config.adaptive_air_enabled)b->out.damper=DAMPER_BYPASS;
}
static void Enter(BurnerControl *b, BurnerState state, uint32_t now)
{
    b->state = state;
    b->state_ms = now;
    b->tracking_flame = b->tracking_purge = false;
}
static bool Fresh(const BurnerControl *b, const BurnerInputs *in, uint32_t now)
{ return in && !Elapsed(now, in->captured_ms, b->config.input_max_age_ms); }
static bool AutoAllowed(const BurnerControl *b, const BurnerInputs *in,bool starting)
{
    uint16_t start_limit=b->co2_target_ppm>b->config.co2_hysteresis_ppm?
        (uint16_t)(b->co2_target_ppm-b->config.co2_hysteresis_ppm):0U;
    return b->co2_target_ppm && in->co2_valid && in->daylight_valid && in->daylight &&
        (starting?in->co2_ppm<=start_limit:in->co2_ppm<b->co2_target_ppm);
}
static void Trip(BurnerControl *b, BurnerFault fault, const BurnerInputs *in, uint32_t now)
{
    FuelOff(b);
    if (b->fault == BURNER_NO_FAULT) {
        b->fault = fault;
        b->fault_source = b->cycle_source;
        b->fault_sequence++;
        b->fault_ms = now;
        b->fault_purge_active = b->config.fault_postpurge_permitted &&
            in && in->ventilation_safe && Fresh(b, in, now);
    }
    b->out.combustion_fan = b->fault_purge_active;
    Enter(b, BURNER_LOCKOUT, now);
}
bool Burner_AutoStartRequested(const BurnerControl *b,const BurnerInputs *in,uint32_t now,bool armed)
{
    return b&&in&&armed&&b->mode==BURNER_AUTO_MODE&&b->state==BURNER_IDLE&&
        b->fault==BURNER_NO_FAULT&&Burner_ConfigValid(&b->config)&&Fresh(b,in,now)&&
        AutoAllowed(b,in,true)&&in->interlocks_ok&&in->detector_healthy&&
        in->airflow_proof_valid&&in->ventilation_safe&&!in->airflow&&!in->flame&&
        in->fuel_off_proven&&in->ignition_off_proven&&in->fan_stopped_proven&&in->bypass_proven;
}
void Burner_Init(BurnerControl *b, const BurnerConfig *config, uint32_t now)
{
    memset(b, 0, sizeof(*b));
    if (config) b->config = *config;
    b->state = BURNER_IDLE;
    b->state_ms = now;
    b->co2_target_ppm = 800U; /* UI target; NOT a combustion safety limit. */
}
BurnerCommandResult Burner_RequestStart(BurnerControl *b, BurnerSource source,
                                       const BurnerInputs *in, uint32_t now)
{
    if ((unsigned)source > BURNER_AUTO) return BURNER_COMMAND_INHIBITED;
    if (b->state != BURNER_IDLE || b->fault != BURNER_NO_FAULT)
        return BURNER_COMMAND_BUSY;
    b->last_command_source = source;
    if (!Burner_ConfigValid(&b->config)) return BURNER_COMMAND_CONFIG_REQUIRED;
    if (!Fresh(b, in, now) || (b->mode == BURNER_AUTO_MODE && !AutoAllowed(b, in,true)))
        return BURNER_COMMAND_INHIBITED;
    b->cycle_source = source;
    b->attempt = 0U;
    b->air_adjustments=0;b->sample_attempts=0;b->have_previous_adjust_co=false;
    b->out.air_percent=b->config.air_initial_percent;b->out.exchanger_fan=true;
    FuelOff(b);
    Enter(b, BURNER_PRECHECK, now);
    return BURNER_COMMAND_ACCEPTED;
}
void Burner_RequestStop(BurnerControl *b, BurnerSource source, uint32_t now)
{
    if ((unsigned)source > BURNER_AUTO) return;
    b->last_command_source = source;
    FuelOff(b); /* Immediate logical cut: no additional Process tick required. */
    if (b->state == BURNER_LOCKOUT || b->state == BURNER_IDLE ||
        b->state == BURNER_SHUTDOWN || b->state == BURNER_POSTPURGE) return;
    b->out.combustion_fan = true;
    Enter(b, BURNER_SHUTDOWN, now);
}
void Burner_ReportActuationFault(BurnerControl *b,const BurnerInputs *in,uint32_t now)
{
    if(!b||b->state==BURNER_IDLE||b->state==BURNER_LOCKOUT)return;
    Trip(b,BURNER_ACTUATION,in,now);
}
bool Burner_AcknowledgeFault(BurnerControl *b, const BurnerInputs *in, uint32_t now)
{
    if (b->state != BURNER_LOCKOUT || b->fault_purge_active ||
        !Burner_ConfigValid(&b->config) || !Fresh(b, in, now) ||
        !in->interlocks_ok || !in->detector_healthy || in->flame ||
        !in->airflow_proof_valid || in->airflow || !in->fuel_off_proven ||
        !in->ignition_off_proven || !in->fan_stopped_proven) return false;
    b->fault = BURNER_NO_FAULT;
    memset(&b->out, 0, sizeof(b->out));
    Enter(b, BURNER_IDLE, now);
    return true; /* Does not start. Caller must explicitly request a new cycle. */
}
static void StartAttempt(BurnerControl *b, uint32_t now)
{
    b->attempt++;
    Enter(b, b->attempt == 1U ? BURNER_IGNITION_1 : BURNER_IGNITION_2, now);
    b->out.gas = b->out.ignition = b->out.combustion_fan = true;
}
static bool FlameTemperatureOK(const BurnerInputs *in)
{ return in->flame_temp_valid && in->flame_temp_q4 >= 2800 && in->flame_temp_q4 <= 4000; }
static void SampleEnter(BurnerControl *b, BurnerSampleState state, uint32_t now)
{ b->sample_state = state; b->sample_ms = now; }
static void Sampling(BurnerControl *b, const BurnerInputs *in, uint32_t now)
{
    if(b->config.adaptive_air_enabled) {
        BurnerFault fault=CombustionCycle_Process(b,in,now);
        if(fault!=BURNER_NO_FAULT)Trip(b,fault,in,now);
        return;
    }
    if (b->sample_state == SAMPLE_IDLE) {
        b->out.damper = DAMPER_BYPASS;
        SampleEnter(b, SAMPLE_WAIT_BYPASS, now);
    }
    if (b->sample_state == SAMPLE_WAIT_BYPASS) {
        if (in->bypass_proven) SampleEnter(b, SAMPLE_WAIT_TEMPERATURE, now);
        return;
    }
    if (b->sample_state == SAMPLE_ACCEPTED) {
        if (!in->co_valid || (uint32_t)(now - in->co_sample_ms) >= b->config.input_max_age_ms) {
            CancelSampling(b);
            b->out.damper = DAMPER_BYPASS;
            return;
        }
        if (in->co_ppm > b->config.co_limit_ppm) {
            Trip(b, BURNER_CO_HIGH, in, now);
            return;
        }
    }
    if (!FlameTemperatureOK(in)) {
        CancelSampling(b); /* Never authorize process routing using old CO. */
        b->out.damper = DAMPER_BYPASS;
        return;
    }
    switch (b->sample_state) {
    case SAMPLE_WAIT_TEMPERATURE:
        b->out.sample_valve_1 = true;
        SampleEnter(b, SAMPLE_VALVE_1, now);
        break;
    case SAMPLE_VALVE_1:
        if (Elapsed(now, b->sample_ms, 10000U)) {
            b->out.sample_valve_1 = false;
            b->out.cooler_fan = true;
            SampleEnter(b, SAMPLE_COOLING, now);
        }
        break;
    case SAMPLE_COOLING:
        if ((b->config.cooling_mode == COOL_BY_TIME && Elapsed(now, b->sample_ms, 180000U)) ||
            (b->config.cooling_mode == COOL_BY_TEMPERATURE && in->cooler_temp_valid &&
             in->cooler_temp_q4 < 160)) {
            b->out.cooler_fan = false;
            b->out.sample_valve_2 = true;
            SampleEnter(b, SAMPLE_VALVE_2, now);
        }
        break;
    case SAMPLE_VALVE_2:
        if (Elapsed(now, b->sample_ms, 10000U)) {
            b->out.sample_valve_2 = false;
            SampleEnter(b, SAMPLE_WAIT_CO, now);
        }
        break;
    case SAMPLE_WAIT_CO:
        if (Elapsed(now, b->sample_ms, b->config.co_timeout_ms)) {
            Trip(b, BURNER_CO_TIMEOUT, in, now);
        } else if (Elapsed(now, b->sample_ms, b->config.co_min_wait_ms) && in->co_valid &&
                   (uint32_t)(in->co_sample_ms - b->sample_ms) < INT32_MAX &&
                   (uint32_t)(now - in->co_sample_ms) < b->config.input_max_age_ms) {
            if (in->co_ppm > b->config.co_limit_ppm) Trip(b, BURNER_CO_HIGH, in, now);
            else {
                b->out.damper = DAMPER_PROCESS;
                SampleEnter(b, SAMPLE_ACCEPTED, now);
            }
        }
        break;
    default: break;
    }
}
void Burner_Process(BurnerControl *b, const BurnerInputs *in, uint32_t now)
{
    if (b->state == BURNER_IDLE) return;
    if (b->state == BURNER_LOCKOUT) {
        FuelOff(b);
        if (!Fresh(b, in, now) || !in->ventilation_safe ||
            Elapsed(now, b->fault_ms, b->config.postpurge_ms)) b->fault_purge_active = false;
        b->out.combustion_fan = b->fault_purge_active;
        return;
    }
    if (!Burner_ConfigValid(&b->config)) { Trip(b, BURNER_CONFIGURATION, in, now); return; }
    if (!Fresh(b, in, now)) { Trip(b, BURNER_INPUT_STALE, in, now); return; }
    if (!in->interlocks_ok) { Trip(b, BURNER_INTERLOCK, in, now); return; }
    if (!in->detector_healthy) { Trip(b, BURNER_DETECTOR, in, now); return; }
    if (!in->airflow_proof_valid || !in->ventilation_safe) {
        Trip(b, BURNER_AIRFLOW, in, now); return;
    }
    if (b->state != BURNER_PRECHECK && b->state != BURNER_PURGE &&
        b->state != BURNER_SHUTDOWN && b->state != BURNER_POSTPURGE && !in->airflow) {
        Trip(b, BURNER_AIRFLOW, in, now); return;
    }
    if (b->state == BURNER_RUNNING && !in->flame) {
        Trip(b, BURNER_FLAME_LOST, in, now); return;
    }
    if (b->mode == BURNER_AUTO_MODE && !AutoAllowed(b, in,false) &&
        b->state != BURNER_SHUTDOWN && b->state != BURNER_POSTPURGE)
        Burner_RequestStop(b, BURNER_AUTO, now);
    switch (b->state) {
    case BURNER_PRECHECK:
        if (in->flame) { Trip(b, BURNER_FALSE_FLAME, in, now); break; }
        if (in->airflow || !in->fan_stopped_proven || !in->fuel_off_proven ||
            !in->ignition_off_proven) { Trip(b, BURNER_INTERLOCK, in, now); break; }
        b->out.combustion_fan = true;
        Enter(b, BURNER_PURGE, now);
        break;
    case BURNER_PURGE:
    case BURNER_INTERPURGE:
        if (in->flame) { Trip(b, BURNER_FALSE_FLAME, in, now); break; }
        if (!in->fuel_off_proven || !in->ignition_off_proven) {
            Trip(b, BURNER_INTERLOCK, in, now); break;
        }
        if (!in->airflow) {
            if (b->tracking_purge || b->state == BURNER_INTERPURGE ||
                Elapsed(now, b->state_ms, b->config.airflow_timeout_ms))
                Trip(b, BURNER_AIRFLOW, in, now);
            break;
        }
        if (!b->tracking_purge) { b->tracking_purge = true; b->purge_ms = now; }
        if (Elapsed(now, b->purge_ms, b->state == BURNER_PURGE ?
                    b->config.purge_ms : b->config.interpurge_ms)) StartAttempt(b, now);
        break;
    case BURNER_IGNITION_1:
    case BURNER_IGNITION_2:
        /* Deadline wins: late scheduling must never extend the fuel trial. */
        if (Elapsed(now, b->state_ms, b->config.ignition_ms)) {
            FuelOff(b);
            if (b->attempt >= b->config.max_attempts) Trip(b, BURNER_IGNITION_FAILED, in, now);
            else Enter(b, BURNER_INTERPURGE, now);
            break;
        }
        if (!in->flame) b->tracking_flame = false;
        else if (!b->tracking_flame) { b->tracking_flame = true; b->flame_ms = now; }
        else if (Elapsed(now, b->flame_ms, b->config.flame_stable_ms)) {
            b->out.ignition = false;
            Enter(b, BURNER_RUNNING, now);
            Sampling(b, in, now);
        }
        break;
    case BURNER_RUNNING:
        if (!in->flame) Trip(b, BURNER_FLAME_LOST, in, now);
        else Sampling(b, in, now);
        break;
    case BURNER_SHUTDOWN:
        Enter(b, BURNER_POSTPURGE, now);
        break;
    case BURNER_POSTPURGE:
        if(in->airflow)b->tracking_purge=true;
        if (Elapsed(now, b->state_ms, b->config.postpurge_ms)) {
            b->out.combustion_fan = false;
            b->out.exchanger_fan=false;
            /* IDLE indicates completed shutdown only with real off feedback. */
            if (in->fuel_off_proven && in->ignition_off_proven && in->fan_stopped_proven)
                Enter(b, BURNER_IDLE, now);
        } else if (!in->airflow && (b->tracking_purge||Elapsed(now,b->state_ms,b->config.airflow_timeout_ms)))
            Trip(b, BURNER_AIRFLOW, in, now);
        break;
    default: break;
    }
}
const char *Burner_StateText(BurnerState state)
{
    static const char *const names[] = { "IDLE", "PRECHECK", "PURGING", "IGNITION 1",
        "INTERPURGE", "IGNITION 2", "RUNNING", "SHUTTING DOWN", "POSTPURGE", "LOCKOUT" };
    return (unsigned)state < sizeof(names)/sizeof(names[0]) ? names[state] : "INVALID";
}
const char *Burner_FaultText(BurnerFault fault)
{
    static const char *const names[]={"NONE","CONFIGURATION","INTERLOCK","DETECTOR",
        "FALSE_FLAME","AIRFLOW","IGNITION_FAILED","FLAME_LOST","INPUT_STALE","CO_HIGH","CO_TIMEOUT",
        "ADJUST_LIMIT","COOLING_TIMEOUT","TEMPERATURE","ACTUATION"};
    return (unsigned)fault<sizeof(names)/sizeof(names[0])?names[fault]:"INVALID";
}
const char *Burner_SampleText(BurnerSampleState state)
{
    static const char *const names[]={"IDLE","WAIT_BYPASS","WAIT_TEMPERATURE","VALVE_1",
        "COOLING","VALVE_2","WAIT_CO","ACCEPTED","AIR_SETTLE"};
    return (unsigned)state<sizeof(names)/sizeof(names[0])?names[state]:"INVALID";
}
