#include "BurnerControl.h"
#include "CombustionCycle.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Arbitrary simulation values ONLY, not an approved burner timing profile. */
static BurnerConfig Config(void)
{
    return (BurnerConfig){.commissioned=true,.purge_ms=30000,.required_purge_ms=30000,
        .interpurge_ms=30000,.postpurge_ms=20000,.ignition_ms=5000,.flame_stable_ms=200,
        .airflow_timeout_ms=5000,.input_max_age_ms=100,.co_min_wait_ms=1000,
        .co_timeout_ms=60000,.co_limit_ppm=25,.co2_hysteresis_ppm=100,.max_attempts=2,
        .fault_postpurge_permitted=true,.cooling_mode=COOL_BY_TIME};
}
static BurnerInputs Safe(void)
{
    return (BurnerInputs){.interlocks_ok=true,.detector_healthy=true,
        .airflow_proof_valid=true,.ventilation_safe=true,.fuel_off_proven=true,
        .ignition_off_proven=true,.fan_stopped_proven=true,.bypass_proven=true,
        .flame_temp_valid=true,.flame_temp_q4=3200,.cooler_temp_valid=true,
        .cooler_temp_q4=120,.co2_valid=true,.co2_ppm=500,
        .daylight_valid=true,.daylight=true};
}
static void Tick(BurnerControl *b, BurnerInputs *in, uint32_t now)
{ in->captured_ms=now; Burner_Process(b,in,now); }
static uint32_t Ignite(BurnerControl *b, BurnerInputs *in, uint32_t base)
{
    BurnerConfig c=Config();
    *in=Safe(); in->captured_ms=base;
    Burner_Init(b,&c,base);
    assert(Burner_RequestStart(b,BURNER_PLC,in,base)==BURNER_COMMAND_ACCEPTED);
    assert(!b->out.gas);
    Tick(b,in,base);
    assert(b->state==BURNER_PURGE);
    Tick(b,in,base+4999U);
    assert(!b->out.gas);
    in->airflow=true; in->fan_stopped_proven=false;
    Tick(b,in,base+5000U);
    Tick(b,in,base+34999U);
    assert(b->state==BURNER_PURGE && !b->out.gas);
    Tick(b,in,base+35000U);
    assert(b->state==BURNER_IGNITION_1 && b->out.gas && b->out.ignition);
    assert(Burner_RequestStart(b,BURNER_HMI,in,base+35000U)==BURNER_COMMAND_BUSY);
    assert(b->cycle_source==BURNER_PLC && b->attempt==1U);
    return base+35000U;
}
static uint32_t Running(BurnerControl *b, BurnerInputs *in, uint32_t base)
{
    uint32_t now=Ignite(b,in,base);
    in->flame=true;
    Tick(b,in,now+1U);
    Tick(b,in,now+201U);
    assert(b->state==BURNER_RUNNING && b->out.gas && !b->out.ignition);
    assert(b->out.damper==DAMPER_BYPASS);
    return now+201U;
}
static void Shutdown(uint32_t base)
{
    BurnerControl b; BurnerInputs in;
    uint32_t now=Running(&b,&in,base);
    Burner_RequestStop(&b,BURNER_PLC,now);
    assert(b.state==BURNER_SHUTDOWN && !b.out.gas && !b.out.ignition && b.out.combustion_fan);
    Tick(&b,&in,now);
    assert(b.state==BURNER_POSTPURGE);
    Burner_RequestStop(&b,BURNER_PLC,now+5000U);
    assert(b.state_ms==now); /* Repeated stop cannot restart postpurge. */
    Tick(&b,&in,now+19999U);
    assert(b.out.combustion_fan);
    Tick(&b,&in,now+20000U);
    assert(!b.out.combustion_fan && b.state==BURNER_POSTPURGE);
    in.airflow=false; in.flame=false; in.fan_stopped_proven=true;
    Tick(&b,&in,now+20001U);
    assert(b.state==BURNER_IDLE);
}
static void RetryAndReset(void)
{
    BurnerControl b; BurnerInputs in;
    uint32_t now=Ignite(&b,&in,0);
    Tick(&b,&in,now+5000U);
    assert(!b.out.gas && b.state==BURNER_INTERPURGE);
    Tick(&b,&in,now+5001U);
    Tick(&b,&in,now+35000U);
    assert(!b.out.gas);
    Tick(&b,&in,now+35001U);
    assert(b.state==BURNER_IGNITION_2 && b.attempt==2);
    Tick(&b,&in,now+40001U);
    assert(b.state==BURNER_LOCKOUT && b.fault==BURNER_IGNITION_FAILED && !b.out.gas);
    assert(b.fault_source==BURNER_PLC && b.fault_sequence==1);
    assert(Burner_RequestStart(&b,BURNER_AUTO,&in,now+40001U)==BURNER_COMMAND_BUSY);
    Burner_RequestStop(&b,BURNER_HMI,now+40002U);
    assert(b.fault_source==BURNER_PLC && b.fault_sequence==1);
    Tick(&b,&in,now+60002U);
    assert(!b.out.combustion_fan);
    assert(!Burner_AcknowledgeFault(&b,&in,now+60002U));
    in.airflow=false; in.fan_stopped_proven=true;
    assert(Burner_AcknowledgeFault(&b,&in,now+60002U));
    assert(b.state==BURNER_IDLE && !b.out.gas);
}
static void Faults(void)
{
    BurnerControl b; BurnerInputs in;
    uint32_t now=Running(&b,&in,0);
    b.mode=BURNER_AUTO_MODE; in.flame=false; in.daylight=false;
    Tick(&b,&in,now+1U);
    assert(b.state==BURNER_LOCKOUT && b.fault==BURNER_FLAME_LOST && !b.out.gas);
    now=Running(&b,&in,0); in.interlocks_ok=false;
    Tick(&b,&in,now+1U);
    assert(b.fault==BURNER_INTERLOCK && !b.out.gas);
    now=Running(&b,&in,0); in.airflow=false;
    Tick(&b,&in,now+1U);
    assert(b.fault==BURNER_AIRFLOW && !b.out.gas);
    now=Running(&b,&in,0);
    Burner_Process(&b,&in,now+100U);
    assert(b.fault==BURNER_INPUT_STALE && !b.out.gas);
    now=Ignite(&b,&in,0); in.flame=true;
    Tick(&b,&in,now+4999U); Tick(&b,&in,now+5200U);
    assert(!b.out.gas && b.state==BURNER_INTERPURGE); /* Late flame cannot extend trial. */
    BurnerConfig c=Config(); Burner_Init(&b,&c,0); in=Safe(); in.flame=true;
    assert(Burner_RequestStart(&b,BURNER_HMI,&in,0)==BURNER_COMMAND_ACCEPTED);
    Tick(&b,&in,0); assert(b.fault==BURNER_FALSE_FLAME && !b.out.gas);
    Burner_Init(&b,&c,0); in=Safe();
    assert(Burner_RequestStart(&b,BURNER_HMI,&in,0)==BURNER_COMMAND_ACCEPTED);
    Tick(&b,&in,0); in.fuel_off_proven=false;
    Tick(&b,&in,1); assert(b.fault==BURNER_INTERLOCK && !b.out.gas);
}
static void Modes(void)
{
    BurnerControl b; BurnerInputs in;
    uint32_t now=Running(&b,&in,0);
    in.co2_ppm=1000; in.daylight=false;
    Tick(&b,&in,now+1U); assert(b.state==BURNER_RUNNING);
    b.mode=BURNER_AUTO_MODE;
    Tick(&b,&in,now+2U);
    assert(b.state==BURNER_POSTPURGE && !b.out.gas && b.last_command_source==BURNER_AUTO);
    now=Running(&b,&in,0); b.mode=BURNER_AUTO_MODE; in.co2_valid=false;
    Tick(&b,&in,now+1U); assert(!b.out.gas);
    BurnerConfig c=Config();Burner_Init(&b,&c,0);b.mode=BURNER_AUTO_MODE;in=Safe();
    in.co2_ppm=701;
    assert(Burner_RequestStart(&b,BURNER_AUTO,&in,0)==BURNER_COMMAND_INHIBITED);
    in.co2_ppm=700;
    assert(Burner_RequestStart(&b,BURNER_AUTO,&in,0)==BURNER_COMMAND_ACCEPTED);
    now=Running(&b,&in,0);b.mode=BURNER_AUTO_MODE;
    in.co2_ppm=799;Tick(&b,&in,now+1U);assert(b.state==BURNER_RUNNING&&b.out.gas);
    in.co2_ppm=800;Tick(&b,&in,now+2U);assert(!b.out.gas&&b.state==BURNER_POSTPURGE);
    Burner_Init(&b,&c,0);b.mode=BURNER_AUTO_MODE;b.co2_target_ppm=100;in=Safe();in.co2_ppm=500;
    assert(Burner_RequestStart(&b,BURNER_AUTO,&in,0)==BURNER_COMMAND_INHIBITED); /* no uint16 underflow */
}
static uint32_t SampleToCO(BurnerControl *b, BurnerInputs *in)
{
    uint32_t now=Running(b,in,0);
    Tick(b,in,now+1U); assert(b->out.sample_valve_1);
    Tick(b,in,now+10000U); assert(b->out.sample_valve_1);
    Tick(b,in,now+10001U); assert(!b->out.sample_valve_1 && b->out.cooler_fan);
    Tick(b,in,now+190000U); assert(b->out.cooler_fan);
    Tick(b,in,now+190001U); assert(b->out.sample_valve_2 && !b->out.cooler_fan);
    Tick(b,in,now+200001U); assert(!b->out.sample_valve_2);
    return now+200001U;
}
static void Samples(void)
{
    BurnerControl b; BurnerInputs in;
    uint32_t now=SampleToCO(&b,&in);
    in.co_valid=true; in.co_ppm=10; in.co_sample_ms=now-1U;
    Tick(&b,&in,now+1000U); assert(b.out.damper!=DAMPER_PROCESS);
    in.co_sample_ms=now+1001U;
    Tick(&b,&in,now+1001U); assert(b.out.damper==DAMPER_PROCESS);
    Tick(&b,&in,now+1101U); assert(b.out.damper==DAMPER_BYPASS);
    now=SampleToCO(&b,&in); in.co_valid=true; in.co_ppm=26; in.co_sample_ms=now+1000U;
    Tick(&b,&in,now+1000U); assert(b.fault==BURNER_CO_HIGH && !b.out.gas);
    now=SampleToCO(&b,&in);
    Tick(&b,&in,now+60000U); assert(b.fault==BURNER_CO_TIMEOUT);
    now=Running(&b,&in,0); b.config.cooling_mode=COOL_BY_TEMPERATURE;
    Tick(&b,&in,now+1U); Tick(&b,&in,now+10001U);
    in.cooler_temp_valid=false; Tick(&b,&in,now+10002U); assert(b.out.cooler_fan);
    in.cooler_temp_valid=true; in.cooler_temp_q4=160;
    Tick(&b,&in,now+10003U); assert(b.out.cooler_fan);
    in.cooler_temp_q4=159; Tick(&b,&in,now+10004U); assert(b.out.sample_valve_2);
    Burner_RequestStop(&b,BURNER_HMI,now+10005U);
    assert(!b.out.sample_valve_1 && !b.out.sample_valve_2 && !b.out.cooler_fan);
}
static void Readiness(void)
{
    BurnerControl b;BurnerConfig c=Config();BurnerInputs in=Safe();
    Burner_Init(&b,&c,0);
    BurnerHardwareStatus h={.proof_inputs_connected=true,.fan_calibrated=true,
        .fan_sync=true,.damper_referenced=true,.co_calibrated=true};
    /* MANUAL does not depend on radio CO2, daylight, PLC or mixer NTC. */
    in.co2_valid=in.daylight_valid=in.daylight=false;
    assert(Burner_StartBlockers(&b,&in,&h,0)==0);
    b.mode=BURNER_AUTO_MODE;
    assert(Burner_StartBlockers(&b,&in,&h,0)==(BURNER_BLOCK_CO2|BURNER_BLOCK_DAYLIGHT));
    in=Safe();in.co2_ppm=700;assert(!Burner_StartBlockers(&b,&in,&h,0));
    in.co2_ppm=701;assert(Burner_StartBlockers(&b,&in,&h,0)==BURNER_BLOCK_CO2);
    b.mode=BURNER_MANUAL_MODE;h.proof_inputs_connected=false;
    assert(Burner_StartBlockers(&b,&in,&h,0)==BURNER_BLOCK_PROOF_CONNECTION);
    h.proof_inputs_connected=true;b.config.commissioned=false;
    assert(Burner_ParametersValid(&b.config)&&!Burner_ConfigValid(&b.config));
    assert(Burner_StartBlockers(&b,&in,&h,0)==BURNER_BLOCK_COMMISSIONING);
    b.config.commissioned=true;h.fan_sync=false;
    assert(Burner_StartBlockers(&b,&in,&h,0)==BURNER_BLOCK_FAN_SYNC);
    h.fan_sync=true;in.fuel_off_proven=false;
    assert(Burner_StartBlockers(&b,&in,&h,0)==BURNER_BLOCK_PROOF_STATE);
    in=Safe();in.captured_ms=UINT32_MAX-50U;
    assert(!(Burner_StartBlockers(&b,&in,&h,48)&BURNER_BLOCK_STALE));
    assert(Burner_StartBlockers(&b,&in,&h,49)&BURNER_BLOCK_STALE);
    assert(!strcmp(Burner_StartBlockText(BURNER_BLOCK_PROOF_CONNECTION),"SAFETY INPUTS NOT WIRED"));
    assert(!strcmp(Burner_StartBlockText(1UL<<31),"UNKNOWN START BLOCK"));
    assert(!b.out.gas&&!b.out.ignition); /* Inspection never requests outputs. */
}
static void AdaptiveFreshness(void)
{
    BurnerControl b={0};BurnerInputs in=Safe();
    b.config.input_max_age_ms=100;b.config.co_trip_ppm=50;b.config.co_limit_ppm=10;
    b.config.sample_period_ms=300000;b.config.damper_timeout_ms=15000;
    for(unsigned wrap=0;wrap<2;++wrap) {
        uint32_t now=wrap?25U:1000U;
        b.sample_state=SAMPLE_ACCEPTED;b.sample_ms=now-20;
        b.out.damper=DAMPER_PROCESS;b.out.distribution_fan=true;
        in.co_valid=true;in.co_ppm=4;in.co_sample_ms=now-99;in.process_proven=true;
        assert(CombustionCycle_Process(&b,&in,now)==BURNER_NO_FAULT&&b.out.distribution_fan);
        assert(CombustionCycle_Process(&b,&in,now+1)==BURNER_NO_FAULT);
        assert(b.out.damper==DAMPER_BYPASS&&!b.out.distribution_fan);
    }
    in.flame_temp_q4=4001;b.sample_state=SAMPLE_COOLING;
    assert(CombustionCycle_Process(&b,&in,2000)==BURNER_TEMPERATURE);
}
int main(void)
{
    Readiness();AdaptiveFreshness();
    BurnerConfig c=Config(); BurnerControl b; BurnerInputs in=Safe();
    assert(Burner_ConfigValid(&c)); c.commissioned=false;
    Burner_Init(&b,&c,0);
    assert(Burner_RequestStart(&b,BURNER_HMI,&in,0)==BURNER_COMMAND_CONFIG_REQUIRED);
    assert(!b.out.gas && !b.out.ignition);
    assert(Burner_FiveVolumePurgeMs(100,1000)==30000);
    assert(Burner_FiveVolumePurgeMs(101,1000)==30300);
    assert(Burner_FiveVolumePurgeMs(1,0)==UINT32_MAX);
    assert(Burner_FiveVolumePurgeMs(UINT32_MAX,1)==UINT32_MAX);
    c=Config();Burner_Init(&b,&c,0);in=Safe();b.mode=BURNER_AUTO_MODE;
    in.bypass_proven=true;in.co2_ppm=700;
    assert(Burner_AutoStartRequested(&b,&in,0,true));
    assert(!Burner_AutoStartRequested(&b,&in,0,false));
    in.co2_ppm=701;assert(!Burner_AutoStartRequested(&b,&in,0,true));in.co2_ppm=700;
    in.daylight=false;assert(!Burner_AutoStartRequested(&b,&in,0,true));in.daylight=true;
    in.co2_valid=false;assert(!Burner_AutoStartRequested(&b,&in,0,true));in.co2_valid=true;
    in.bypass_proven=false;assert(!Burner_AutoStartRequested(&b,&in,0,true));in.bypass_proven=true;
    b.state=BURNER_LOCKOUT;assert(!Burner_AutoStartRequested(&b,&in,0,true));
    Shutdown(0); Shutdown(UINT32_MAX-10000U);
    RetryAndReset(); Faults(); Modes(); Samples();
    uint32_t now=Running(&b,&in,0);
    Burner_ReportActuationFault(&b,&in,now+1);
    assert(b.state==BURNER_LOCKOUT&&b.fault==BURNER_ACTUATION&&!b.out.gas&&!b.out.ignition);
    uint32_t sequence=b.fault_sequence;
    Burner_ReportActuationFault(&b,&in,now+2);assert(b.fault_sequence==sequence);
    assert(Burner_RequestStart(&b,BURNER_HMI,&in,now+3)==BURNER_COMMAND_BUSY);
    puts("Burner supervisory tests passed (simulation only; no approved burner profile).");
    return 0;
}
