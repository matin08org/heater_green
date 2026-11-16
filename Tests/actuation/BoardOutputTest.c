#include "BoardOutputTest.h"
#include "HC595.h"
#include "DamperStepper.h"
#include <string.h>
static BoardOutputTestSnapshot status;
static uint32_t armed_ms,start_ms,bus_errors;
static void Off(void)
{
 BoardActuation_Apply(NULL,false,0);
 status.armed=status.active=status.operator_confirmed_dummy_loads=false;
}
static bool Fail(BoardOutputTestReason why)
{status.reason=why;status.stop_requested=true;Off();return true;}
void BoardOutputTest_Init(void)
{memset(&status,0,sizeof(status));Off();}
void BoardOutputTest_Get(BoardOutputTestSnapshot *out){if(out)*out=status;}
void BoardOutputTest_Disarm(void)
{Off();status.stop_requested=false;status.reason=OUTPUT_TEST_NONE;}
bool BoardOutputTest_Arm(bool confirmed,uint32_t now)
{
 BoardActuationSnapshot act;BoardActuation_Get(&act);
 HC595_Diagnostics bus;HC595_GetDiagnostics(&bus);
 const BoardOutputTestConfig *c=&board_output_test_config;
 if(!c->enabled||!c->require_dummy_load_confirmation||!confirmed||status.armed||
    !act.referenced||!act.bypass_proven||act.moving||act.fault||!act.fan_sync||DamperStepper_IsRunning()||
    !bus.initialized||(bus.shadow&0xfeU)||(bus.latched&0xfeU)||
    c->fan_on_ccr>333U||!c->arm_timeout_ms||!c->max_run_ms||c->max_total_ms<=c->max_run_ms) {
  status.reason=OUTPUT_TEST_NOT_READY;return false;
 }
 memset(&status,0,sizeof(status));status.armed=true;
 status.operator_confirmed_dummy_loads=true;armed_ms=now;bus_errors=bus.errors;return true;
}
bool BoardOutputTest_Process(const BurnerOutputs *out,BurnerState state,uint32_t now)
{
 if(!status.armed){BoardActuation_Apply(NULL,false,now);return false;}
 if(!out)return Fail(OUTPUT_TEST_INVALID_REQUEST);
 if(!status.active) {
  if(state==BURNER_IDLE) {
   if(now-armed_ms>=board_output_test_config.arm_timeout_ms)return Fail(OUTPUT_TEST_ARM_EXPIRED);
   BoardActuation_Apply(NULL,false,now);return false;
  }
  if(state!=BURNER_PRECHECK&&state!=BURNER_PURGE)return Fail(OUTPUT_TEST_INVALID_REQUEST);
  status.active=true;start_ms=now;
 }
 status.elapsed_ms=now-start_ms;
 if(status.elapsed_ms>=board_output_test_config.max_total_ms)return Fail(OUTPUT_TEST_TOTAL_LIMIT);
 if(state==BURNER_IDLE||(state==BURNER_LOCKOUT&&!out->combustion_fan)){Off();return false;}
 BoardActuationSnapshot act;BoardActuation_Get(&act);
 HC595_Diagnostics bus;HC595_GetDiagnostics(&bus);
 if(!act.fan_sync)return Fail(OUTPUT_TEST_SYNC_LOST);
 if(act.fault||!act.referenced)return Fail(OUTPUT_TEST_MOTION_FAULT);
 if(bus.errors!=bus_errors)return Fail(OUTPUT_TEST_SPI_ERROR);
 if((out->gas&&(state!=BURNER_IGNITION_1&&state!=BURNER_IGNITION_2&&state!=BURNER_RUNNING))||
    (out->sample_valve_1&&out->sample_valve_2))return Fail(OUTPUT_TEST_INVALID_REQUEST);
 if(status.elapsed_ms>=board_output_test_config.max_run_ms&&state!=BURNER_SHUTDOWN&&
    state!=BURNER_POSTPURGE&&state!=BURNER_LOCKOUT) {
  BurnerOutputs off=*out;off.gas=off.ignition=false;
  off.sample_valve_1=off.sample_valve_2=off.cooler_fan=off.distribution_fan=false;
  off.damper=DAMPER_HOLD;status.stop_requested=true;
  BoardActuation_ApplyOutputTest(&off,true,board_output_test_config.fan_on_ccr,now);return true;
 }
 BoardActuation_ApplyOutputTest(out,true,board_output_test_config.fan_on_ccr,now);
 BoardActuation_Get(&act);
 if(act.fault||act.inhibited)return Fail(OUTPUT_TEST_MOTION_FAULT);
 return false;
}
