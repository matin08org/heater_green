#include "BurnerControl.h"
#include "scenario-profile.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Host only. No HAL / GPIO is linked; all inputs and proofs below are fixtures. */
typedef struct {
 BurnerControl b;BurnerInputs in;
 uint32_t now,elapsed,run_start;bool was_running;
 unsigned flame_attempt;bool flame_loss,cool_fail,co_missing,airflow_loss;
 bool high_once,low_temperature,no_improvement;
 FILE *trace;
} Sim;
static const char *directory;
static BurnerConfig Config(void)
{
 return (BurnerConfig){.commissioned=true,.adaptive_air_enabled=true,.cooling_mode=COOL_BY_TEMPERATURE,
 .purge_ms=SCENARIO_PURGE_MS,.required_purge_ms=SCENARIO_PURGE_MS,
 .ignition_ms=SCENARIO_IGNITION_MS,.postpurge_ms=SCENARIO_POSTPURGE_MS,
 .interpurge_ms=30000,.flame_stable_ms=500,.airflow_timeout_ms=5000,.input_max_age_ms=500,
 .co_min_wait_ms=1000,.co_timeout_ms=60000,.co_limit_ppm=10,.co_accept_ppm=5,.co_trip_ppm=50,
 .co2_hysteresis_ppm=100,.max_attempts=2,.fault_postpurge_permitted=true,
 .initial_measure_delay_ms=1000,.sample_period_ms=300000,.sample_fill_ms=10000,.sample_transfer_ms=10000,
 .cooling_timeout_ms=180000,.air_settle_ms=30000,.damper_timeout_ms=15000,
 .air_initial_percent=50,.air_min_percent=10,.air_max_percent=100,.air_step_percent=10,.max_adjustments=3};
}
static void Init(Sim *s,const char *name,uint32_t base)
{
 memset(s,0,sizeof(*s));s->now=base;s->flame_attempt=1;
 s->in=(BurnerInputs){.captured_ms=base,.interlocks_ok=true,.detector_healthy=true,
 .airflow_proof_valid=true,.ventilation_safe=true,.fuel_off_proven=true,.ignition_off_proven=true,
 .fan_stopped_proven=true,.bypass_proven=true,.flame_temp_valid=true,.flame_temp_q4=3200,
 .cooler_temp_valid=true,.cooler_temp_q4=180,.co2_valid=true,.co2_ppm=500,
 .daylight_valid=true,.daylight=true};
 BurnerConfig c=Config();assert(Burner_ConfigValid(&c));Burner_Init(&s->b,&c,base);
 char path[1024];int n=snprintf(path,sizeof(path),"%s/%s.csv",directory,name);assert(n>0&&(unsigned)n<sizeof(path));
 s->trace=fopen(path,"w");assert(s->trace);
 fputs("virtual_ms,state,sample,gas_request,ignition_request,fan_request,air_percent,damper_request,valve1,valve2,cooler,distribution,fault\n",s->trace);
 assert(Burner_RequestStart(&s->b,BURNER_HMI,&s->in,s->now)==BURNER_COMMAND_ACCEPTED);
}
static void Log(Sim *s)
{
 const BurnerOutputs *o=&s->b.out;
 fprintf(s->trace,"%u,%s,%s,%u,%u,%u,%u,%u,%u,%u,%u,%u,%s\n",s->elapsed,
 Burner_StateText(s->b.state),Burner_SampleText(s->b.sample_state),o->gas,o->ignition,
 o->combustion_fan,o->air_percent,o->damper,o->sample_valve_1,o->sample_valve_2,
 o->cooler_fan,o->distribution_fan,Burner_FaultText(s->b.fault));
}
static void Tick(Sim *s)
{
 BurnerControl *b=&s->b;BurnerInputs *in=&s->in;BurnerOutputs old=b->out;
 BurnerState old_state=b->state;BurnerSampleState old_sample=b->sample_state;
 ++s->now;++s->elapsed;in->captured_ms=s->now;
 in->fuel_off_proven=!b->out.gas;in->ignition_off_proven=!b->out.ignition;
 in->airflow=!s->airflow_loss&&b->out.combustion_fan&&s->elapsed>=100U;
 in->fan_stopped_proven=!b->out.combustion_fan;
 bool ignition=b->state==BURNER_IGNITION_1||b->state==BURNER_IGNITION_2;
 in->flame=!s->flame_loss&&((ignition&&b->attempt>=s->flame_attempt&&
 s->now-b->state_ms>=200U)||(b->state==BURNER_RUNNING));
 in->bypass_proven=b->out.damper==DAMPER_BYPASS;
 in->process_proven=b->out.damper==DAMPER_PROCESS;
 in->flame_temp_q4=s->low_temperature&&b->air_adjustments==0?2700:3200;
 in->cooler_temp_valid=!s->cool_fail;
 in->cooler_temp_q4=b->sample_state==SAMPLE_COOLING&&s->now-b->sample_ms>=2000U?156:180;
 in->co_valid=!s->co_missing&&b->sample_state==SAMPLE_WAIT_CO&&s->now-b->sample_ms>=1000U;
 /* Once accepted, ADC continues to produce fixture samples until next cycle. */
 if(b->sample_state==SAMPLE_ACCEPTED)in->co_valid=!s->co_missing;
 if(in->co_valid){in->co_sample_ms=s->now;in->co_ppm=(s->no_improvement||(s->high_once&&b->air_adjustments==0))?20:4;}
 Burner_Process(b,in,s->now);
 if(b->state==BURNER_RUNNING&&!s->was_running){s->run_start=s->now;s->was_running=true;}
 if(old_state!=b->state||old_sample!=b->sample_state||memcmp(&old,&b->out,sizeof(old)))Log(s);
 /* Physical output invariant is tested here only as a REQUEST invariant. */
 assert(!(b->out.sample_valve_1&&b->out.sample_valve_2));
 if(b->state==BURNER_PURGE||b->state==BURNER_INTERPURGE||b->state==BURNER_LOCKOUT)
   assert(!b->out.gas&&!b->out.ignition);
}
static void UntilState(Sim *s,BurnerState state,uint32_t limit)
{for(uint32_t i=0;i<limit&&s->b.state!=state;i++)Tick(s);
 if(s->b.state!=state)fprintf(stderr,"Expected %s at %ums; got %s fault=%s\n",Burner_StateText(state),s->elapsed,Burner_StateText(s->b.state),Burner_FaultText(s->b.fault));
 assert(s->b.state==state);}
static void UntilSample(Sim *s,BurnerSampleState state,uint32_t limit)
{for(uint32_t i=0;i<limit&&s->b.sample_state!=state&&s->b.state!=BURNER_LOCKOUT;i++)Tick(s);assert(s->b.sample_state==state);}
static void Done(Sim *s,const char *name)
{Log(s);assert(!fclose(s->trace));printf("{\"name\":\"%s\",\"passed\":true,\"virtual_ms\":%u}\n",name,s->elapsed);}
static void Stop(Sim *s,BurnerSource source)
{
 uint32_t now=s->now;Burner_RequestStop(&s->b,source,now);Log(s);
 assert(s->b.state==BURNER_SHUTDOWN&&!s->b.out.gas&&!s->b.out.ignition&&s->b.out.combustion_fan);
 Tick(s);uint32_t purge=s->b.state_ms;assert(s->b.state==BURNER_POSTPURGE);
 Burner_RequestStop(&s->b,source,s->now);assert(s->b.state_ms==purge);
 while(s->now-purge<SCENARIO_POSTPURGE_MS-1U)Tick(s);
 assert(s->b.out.combustion_fan);UntilState(s,BURNER_IDLE,10);
 assert(s->now-now>=60000U&&!s->b.out.gas&&!s->b.out.combustion_fan&&!s->b.out.exchanger_fan);
}
int main(int argc,char **argv)
{
 assert(argc==2);directory=argv[1];Sim s;
 Init(&s,"01_success_periodic_stop",0);UntilState(&s,BURNER_IGNITION_1,40000);
 assert(s.elapsed>=30100U);UntilState(&s,BURNER_RUNNING,5000);
 UntilSample(&s,SAMPLE_ACCEPTED,60000);Tick(&s);assert(s.b.out.distribution_fan);
 uint32_t accepted=s.now;UntilSample(&s,SAMPLE_WAIT_BYPASS,310000);
 assert(s.now-accepted>=299999U&&!s.b.out.distribution_fan&&s.b.out.damper==DAMPER_BYPASS);
 Stop(&s,BURNER_PLC);Done(&s,"01_success_periodic_stop");
 Init(&s,"02_retry_success",0);s.flame_attempt=2;UntilState(&s,BURNER_INTERPURGE,40000);
 uint32_t inter=s.now;UntilState(&s,BURNER_IGNITION_2,40000);assert(s.now-inter>=30000U);
 UntilState(&s,BURNER_RUNNING,5000);assert(s.b.attempt==2);Done(&s,"02_retry_success");
 Init(&s,"03_two_failures_lockout",0);s.flame_attempt=3;UntilState(&s,BURNER_LOCKOUT,80000);
 assert(s.b.attempt==2&&s.b.fault==BURNER_IGNITION_FAILED);
 assert(Burner_RequestStart(&s.b,BURNER_AUTO,&s.in,s.now)==BURNER_COMMAND_BUSY);Done(&s,"03_two_failures_lockout");
 Init(&s,"04_flame_loss",0);UntilState(&s,BURNER_RUNNING,40000);s.flame_loss=true;Tick(&s);
 assert(s.b.fault==BURNER_FLAME_LOST&&!s.b.out.gas);Done(&s,"04_flame_loss");
 Init(&s,"05_stop_during_purge",0);UntilState(&s,BURNER_PURGE,10);Stop(&s,BURNER_HMI);Done(&s,"05_stop_during_purge");
 Init(&s,"06_stop_during_ignition",0);UntilState(&s,BURNER_IGNITION_1,40000);Stop(&s,BURNER_PLC);Done(&s,"06_stop_during_ignition");
 Init(&s,"07_cooling_timeout",0);s.cool_fail=true;UntilState(&s,BURNER_RUNNING,40000);UntilState(&s,BURNER_LOCKOUT,250000);
 assert(s.b.fault==BURNER_COOLING_TIMEOUT);Done(&s,"07_cooling_timeout");
 Init(&s,"08_high_co_then_recovery",0);s.high_once=true;UntilState(&s,BURNER_RUNNING,40000);UntilSample(&s,SAMPLE_ACCEPTED,150000);
 assert(s.b.air_adjustments==1&&s.b.out.air_percent==60);Done(&s,"08_high_co_then_recovery");
 Init(&s,"09_low_temperature_trim",0);s.low_temperature=true;UntilState(&s,BURNER_RUNNING,40000);UntilSample(&s,SAMPLE_ACCEPTED,150000);
 assert(s.b.air_adjustments==1&&s.b.out.air_percent==40);Done(&s,"09_low_temperature_trim");
 Init(&s,"10_uncal_co_no_greenhouse",0);s.co_missing=true;UntilState(&s,BURNER_RUNNING,40000);UntilState(&s,BURNER_LOCKOUT,150000);
 assert(s.b.fault==BURNER_CO_TIMEOUT&&s.b.out.damper==DAMPER_BYPASS&&!s.b.out.distribution_fan);Done(&s,"10_uncal_co_no_greenhouse");
 Init(&s,"11_no_improvement_lockout",0);s.no_improvement=true;UntilState(&s,BURNER_RUNNING,40000);UntilState(&s,BURNER_LOCKOUT,150000);
 assert(s.b.fault==BURNER_ADJUST_LIMIT);Done(&s,"11_no_improvement_lockout");
 Init(&s,"12_auto_target_stop",0);s.b.mode=BURNER_AUTO_MODE;UntilState(&s,BURNER_RUNNING,40000);
 s.in.co2_ppm=800;Tick(&s);assert(!s.b.out.gas&&s.b.state==BURNER_POSTPURGE);Done(&s,"12_auto_target_stop");
 Init(&s,"13_auto_night_stop",0);s.b.mode=BURNER_AUTO_MODE;UntilState(&s,BURNER_RUNNING,40000);
 s.in.daylight=false;Tick(&s);assert(!s.b.out.gas&&s.b.state==BURNER_POSTPURGE);Done(&s,"13_auto_night_stop");
 Init(&s,"14_tick_wrap",UINT32_MAX-10000U);UntilState(&s,BURNER_RUNNING,40000);Stop(&s,BURNER_HMI);Done(&s,"14_tick_wrap");
 Init(&s,"15_repeated_start",0);UntilState(&s,BURNER_PURGE,10);uint32_t start=s.b.state_ms;
 assert(Burner_RequestStart(&s.b,BURNER_PLC,&s.in,s.now)==BURNER_COMMAND_BUSY&&s.b.state_ms==start);
 Done(&s,"15_repeated_start");
 Init(&s,"16_postpurge_airflow_loss",0);UntilState(&s,BURNER_RUNNING,40000);
 Burner_RequestStop(&s.b,BURNER_HMI,s.now);Tick(&s);Tick(&s);assert(s.b.tracking_purge);
 s.airflow_loss=true;Tick(&s);assert(s.b.state==BURNER_LOCKOUT&&s.b.fault==BURNER_AIRFLOW&&!s.b.out.gas);
 Done(&s,"16_postpurge_airflow_loss");return 0;
}
