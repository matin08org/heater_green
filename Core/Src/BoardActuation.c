#include "BoardActuation.h"
#include "Board595Map.h"
#include "DamperStepper.h"
#include "tim.h"
#include <limits.h>
#include <string.h>
static BoardActuationSnapshot s;
static int32_t exhaust,target;
static bool hall_raw,hall_stable,hall_released,sync_seen,completion_pending;
static bool endpoint_hall_required;
static uint32_t hall_ms,last_sync,move_ms,completion_ms;
static uint8_t sync_edges,arm_edges;
static uint8_t relay_test_step;
static uint32_t relay_test_ms;
static uint32_t tuning_ms,tuning_bus_errors;
const char *BoardActuation_InhibitText(uint32_t flags)
{
    if(flags&ACT_BLOCK_FAULT)return "Actuator fault; inspect damper";
    if(flags&ACT_BLOCK_REFERENCE)return "EXHAUST reference not confirmed";
    if(flags&ACT_BLOCK_SYNC)return "Fan zero-cross sync missing";
    if(flags&ACT_BLOCK_CALIBRATION)return "Fan command table not accepted";
    if(flags&ACT_BLOCK_PERMISSION)return "Process permission not granted";
    if(flags&ACT_BLOCK_SERVICE)return "Manual service test owns outputs";
    return "No output inhibit";
}
bool BoardActuation_RelayTestActive(void){return relay_test_step!=0U;}
void BoardActuation_StopRelayTest(void)
{
    if(relay_test_step){relay_test_step=0;(void)HC595_ClearAll();}
}
bool BoardActuation_StartRelayTest(bool idle,uint32_t now)
{
    if(!idle||relay_test_step||s.fan_tuning||DamperStepper_IsRunning())return false;
    if(HC595_ClearAll()!=HAL_OK||HC595_WriteByte(2U)!=HAL_OK)return false;
    relay_test_step=1;relay_test_ms=now;return true;
}
static void FanOff(void)
{
    TIM1->BDTR &= ~TIM_BDTR_MOE; /* immediate gate suppression; CCR is preloaded */
    (void)TIM1_PhaseSetCompare(TIM1_PHASE_COUNTS);
    s.fan_ccr=TIM1_PHASE_COUNTS;arm_edges=0;
}
void BoardActuation_StopFanTuning(void)
{
    if(s.fan_tuning){s.fan_tuning=false;FanOff();}
}
bool BoardActuation_TuneFan(uint16_t compare,bool idle,uint32_t now)
{
    if(compare>TIM1_PHASE_COUNTS)return false;
    if(compare==TIM1_PHASE_COUNTS){BoardActuation_StopFanTuning();return idle;}
    HC595_Diagnostics bus;HC595_GetDiagnostics(&bus);
    if(!idle||s.fault||!s.fan_sync||s.moving||relay_test_step||DamperStepper_IsRunning()||
       !bus.initialized||((bus.shadow|bus.latched)&0xfeU)) {
        BoardActuation_StopFanTuning();return false;
    }
    if(!s.fan_tuning){tuning_ms=now;tuning_bus_errors=bus.errors;}
    if(now-tuning_ms>=120000U||bus.errors!=tuning_bus_errors){BoardActuation_StopFanTuning();return false;}
    bool was_off=s.fan_ccr==TIM1_PHASE_COUNTS;
    if(TIM1_PhaseSetCompare(compare)!=HAL_OK){BoardActuation_StopFanTuning();return false;}
    s.fan_tuning=true;s.fan_ccr=compare;
    if(was_off)arm_edges=2U;
    return true;
}
static void Fail(void)
{
    DamperStepper_Stop();s.referenced=false;s.moving=false;
    s.bypass_proven=s.process_proven=false;s.fault=true;s.inhibited=true;++s.errors;
    s.inhibit_flags=ACT_BLOCK_FAULT;
}
void BoardActuation_Init(void)
{
    relay_test_step=0;
    endpoint_hall_required=false;
    memset(&s,0,sizeof(s));s.inhibited=true;
    const BoardActuationConfig *c=&board_actuation_config;
    s.fan_calibrated=c->fan_calibrated&&c->fan_ccr[0]==TIM1_PHASE_COUNTS;
    for(unsigned i=1;i<11;i++)
        if(c->fan_ccr[i]>333U||c->fan_ccr[i]>c->fan_ccr[i-1])s.fan_calibrated=false;
    hall_raw=hall_stable=false;hall_released=sync_seen=false;
    hall_ms=HAL_GetTick();sync_edges=arm_edges=0;FanOff();
    (void)HC595_ClearAll();
}
void BoardActuation_Get(BoardActuationSnapshot *out){if(out)*out=s;}
void BoardActuation_Service(uint32_t now)
{
    if(relay_test_step&&(uint32_t)(now-relay_test_ms)>=3000U) {
        if(relay_test_step==7U||(uint32_t)(now-relay_test_ms)>=6000U)BoardActuation_StopRelayTest();
        else {
            ++relay_test_step;relay_test_ms=now;
            if(HC595_WriteByte((uint8_t)((1U<<(relay_test_step+1U))-2U))!=HAL_OK)
                BoardActuation_StopRelayTest();
        }
    }
    bool raw=HAL_GPIO_ReadPin(HAL1_OPEN_GPIO_Port,HAL1_OPEN_Pin)==GPIO_PIN_RESET;
    if(raw!=hall_raw){hall_raw=raw;hall_ms=now;}
    if(now-hall_ms>=board_actuation_config.hall_debounce_ms)hall_stable=hall_raw;
    /* Capture is hardware-latched. No GPIO edge polling or timer ISR is needed.
     * CCR2 is the previous half-cycle measured BEFORE the slave reset. */
    if(TIM1->SR&TIM_SR_CC2IF) {
        uint32_t period=TIM1->CCR2;
        s.fan_period_us=period*TIM1_PHASE_TICK_US;
        TIM1->SR=~(TIM_SR_CC2IF|TIM_SR_CC2OF);
        if(sync_seen&&now-last_sync>=25U)sync_edges=0;
        if(period>=300U&&period<=367U) {if(sync_edges<3U)++sync_edges;}
        else {sync_edges=0;FanOff();}
        last_sync=now;sync_seen=true;
        if(arm_edges&&!--arm_edges&&sync_edges>=3U)TIM1->BDTR|=TIM_BDTR_MOE;
    }
    s.fan_sync=sync_seen&&sync_edges>=3U&&now-last_sync<25U;
    s.fan_sync_age_ms=sync_seen?now-last_sync:UINT32_MAX;
    if(sync_seen&&now-last_sync>=25U)sync_edges=0;
    if(!s.fan_sync)FanOff();
    if(s.fan_tuning) {
        HC595_Diagnostics bus;HC595_GetDiagnostics(&bus);
        if(!s.fan_sync||s.fault||s.moving||DamperStepper_IsRunning()||!bus.initialized||
           ((bus.shadow|bus.latched)&0xfeU)||bus.errors!=tuning_bus_errors||now-tuning_ms>=120000U)
            BoardActuation_StopFanTuning();
    }
    if(s.moving) {
        if(!hall_stable)hall_released=true;
        if(now-move_ms>=board_actuation_config.travel_timeout_ms) {Fail();return;}
        if(hall_released&&hall_stable) {
            int64_t error=(int64_t)DamperStepper_GetPosition()-target;
            DamperStepper_Stop(); /* destination magnet: stop before any further scheduled leg */
            if(error<-(int64_t)board_actuation_config.endpoint_tolerance_pulses||
               error>board_actuation_config.endpoint_tolerance_pulses){Fail();return;}
            s.moving=false;endpoint_hall_required=true;
            s.bypass_proven=s.destination==DAMPER_BYPASS;
            s.process_proven=s.destination==DAMPER_PROCESS;
            return;
        }
        if(!DamperStepper_IsRunning()) {
            if(!completion_pending){completion_pending=true;completion_ms=now;}
            if(!hall_released||DamperStepper_GetPosition()!=target){Fail();return;}
            if(!hall_stable) {
                if(now-completion_ms>=board_actuation_config.hall_debounce_ms)Fail();
                return;
            }
            s.moving=false;endpoint_hall_required=true;
            s.bypass_proven=s.destination==DAMPER_BYPASS;
            s.process_proven=s.destination==DAMPER_PROCESS;
        }
    } else if(s.referenced&&endpoint_hall_required&&!hall_stable) {
        /* Loss of an endpoint while stopped invalidates position feedback. */
        Fail();
    }
}
bool BoardActuation_ConfirmExhaust(void)
{
    if(DamperStepper_IsRunning()||board_actuation_config.travel_pulses==0U||
       board_actuation_config.travel_pulses>INT32_MAX||board_actuation_config.travel_timeout_ms==0U)return false;
    int32_t pos=DamperStepper_GetPosition();
    if((int64_t)pos-board_actuation_config.travel_pulses<INT32_MIN)return false;
    exhaust=pos;s.referenced=true;s.fault=false;s.moving=false;
    /* Manual identification is accepted at either Hall level. Only monitor
     * stationary Hall loss if Hall was present at confirmation (or arrival). */
    endpoint_hall_required=hall_stable;
    s.bypass_proven=true;s.process_proven=false;s.destination=DAMPER_BYPASS;return true;
}
void BoardActuation_Abort(void)
{
    DamperStepper_Stop();s.referenced=false;s.moving=false;
    s.bypass_proven=s.process_proven=false;
}
HAL_StatusTypeDef BoardActuation_ManualStep(int32_t delta)
{
    if(s.fan_tuning)return HAL_BUSY;
    if(relay_test_step)return HAL_BUSY;
    if(DamperStepper_IsRunning())return HAL_BUSY;
    int64_t next=(int64_t)DamperStepper_GetPosition()+delta;
    if(next<INT32_MIN||next>INT32_MAX)return HAL_ERROR;
    BoardActuation_Abort();
    HAL_StatusTypeDef status=DamperStepper_SetSpeedRPM(board_actuation_config.rpm);
    if(status==HAL_OK)status=DamperStepper_SetEndpoint((int32_t)next,DAMPER_END_STOP);
    if(status==HAL_OK)status=DamperStepper_Start();
    return status;
}
static void Apply(const BurnerOutputs *out,bool permitted,uint32_t now,bool bench,uint16_t bench_compare)
{
    if(s.fan_tuning) {
        if(out&&!out->gas&&!out->ignition&&!out->combustion_fan&&!out->exchanger_fan&&
           !out->distribution_fan&&!out->cooler_fan&&!out->sample_valve_1&&
           !out->sample_valve_2&&out->damper==DAMPER_HOLD) {s.inhibited=true;s.inhibit_flags=ACT_BLOCK_SERVICE;return;}
        BoardActuation_StopFanTuning();
    }
    if(relay_test_step) {
        /* The service test owns Q1..Q7 only while the process is idle. */
        if(out&&!out->gas&&!out->ignition&&!out->combustion_fan&&
           !out->exchanger_fan&&!out->distribution_fan&&!out->cooler_fan&&
           !out->sample_valve_1&&!out->sample_valve_2&&out->damper==DAMPER_HOLD) {
            FanOff();s.inhibited=true;s.inhibit_flags=ACT_BLOCK_SERVICE;return;
        }
        BoardActuation_StopRelayTest();
    }
    s.inhibited=!permitted;
    bool enabled=out&&permitted&&!s.fault&&s.referenced&&s.fan_sync&&
        (bench?bench_compare<=333U:s.fan_calibrated);
    s.inhibit_flags=0;
    if(s.fault)s.inhibit_flags|=ACT_BLOCK_FAULT;
    if(!out||!permitted)s.inhibit_flags|=ACT_BLOCK_PERMISSION;
    if(!s.referenced)s.inhibit_flags|=ACT_BLOCK_REFERENCE;
    if(!s.fan_sync)s.inhibit_flags|=ACT_BLOCK_SYNC;
    if(bench?bench_compare>333U:!s.fan_calibrated)s.inhibit_flags|=ACT_BLOCK_CALIBRATION;
    if(!enabled) {
        FanOff();(void)HC595_ClearAll();
        if(s.moving)BoardActuation_Abort();
        s.inhibited=true;return;
    }
    if(out->combustion_fan) {
        if(!bench&&(out->air_percent<10U||out->air_percent>100U||out->air_percent%10U)){Fail();FanOff();(void)HC595_ClearAll();return;}
        uint16_t compare=bench?bench_compare:board_actuation_config.fan_ccr[out->air_percent/10U];
        if(s.fan_ccr!=compare) {
            bool was_off=s.fan_ccr==TIM1_PHASE_COUNTS;
            (void)TIM1_PhaseSetCompare(compare);s.fan_ccr=compare;
            if(was_off)arm_edges=2U; /* wait for preload to cross a hardware reset */
        }
    } else FanOff();
    if(out->damper!=DAMPER_HOLD&&out->damper!=s.destination) {
        /* Never reverse a moving linkage without completing/aborting its leg. */
        if(s.moving){Fail();FanOff();(void)HC595_ClearAll();return;}
        target=out->damper==DAMPER_BYPASS?exhaust:exhaust-(int32_t)board_actuation_config.travel_pulses;
        HAL_StatusTypeDef result=DamperStepper_SetSpeedRPM(board_actuation_config.rpm);
        if(result==HAL_OK)result=DamperStepper_SetEndpoint(target,DAMPER_END_STOP);
        if(result==HAL_OK)result=DamperStepper_Start();
        if(result!=HAL_OK){Fail();FanOff();(void)HC595_ClearAll();return;}
        s.destination=out->damper;s.moving=true;s.bypass_proven=s.process_proven=false;
        hall_released=false;completion_pending=false;move_ms=now;++s.moves;
    }
    uint8_t image=0;
    if(out->gas)image|=1U<<BOARD595_GAS_VALVE;
    if(out->ignition)image|=1U<<BOARD595_IGNITION;
    if(out->exchanger_fan)image|=1U<<BOARD595_EXCHANGER_FAN;
    if(out->sample_valve_1)image|=1U<<BOARD595_SAMPLE_VALVE_1;
    if(out->sample_valve_2)image|=1U<<BOARD595_SAMPLE_VALVE_2;
    if(out->cooler_fan)image|=1U<<BOARD595_SAMPLE_COOLER_FAN;
    if(out->distribution_fan&&s.process_proven)image|=1U<<BOARD595_DISTRIBUTION_FAN;
    (void)HC595_WriteByte(image); /* shared shadow preserves Q0 / sensor bus lock */
}
void BoardActuation_Apply(const BurnerOutputs *out,bool permitted,uint32_t now)
{Apply(out,permitted,now,false,TIM1_PHASE_COUNTS);}
void BoardActuation_ApplyOutputTest(const BurnerOutputs *out,bool armed,uint16_t compare,uint32_t now)
{Apply(out,armed,now,true,compare);}
