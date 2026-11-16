#include "BoardScenario.h"
#include <assert.h>
#include <stdio.h>
#ifndef SIM_FLAME
#define SIM_FLAME 1
#endif
#ifndef SIM_CO
#define SIM_CO 1
#endif
const BoardScenarioConfig board_scenario_config={.enabled=true,.simulate_flame=SIM_FLAME,
 .simulate_co=SIM_CO,.fan_on_ccr=0,.fan_off_ccr=1000,.flame_appears_ms=1000};
static uint32_t now;
static void Until(BurnerState state,unsigned limit)
{for(unsigned n=0;n<limit&&BoardScenario_Control()->state!=state;n++)BoardScenario_Process(++now);assert(BoardScenario_Control()->state==state);}
static void Test(uint32_t base)
{
 now=base;BurnerConfig physical={.commissioned=false,.purge_ms=30000,.interpurge_ms=30000,
 .postpurge_ms=60000,.ignition_ms=5000,.flame_stable_ms=500};
 BoardScenario_Init(&physical,now);assert(!physical.commissioned);
 BurnerControl *b=BoardScenario_Control();assert(b->state==BURNER_IDLE&&BoardScenario_FanCompare()==1000);
 assert(BoardScenario_Start(BURNER_HMI,now)==BURNER_COMMAND_ACCEPTED&&b->state==BURNER_PRECHECK);
 Until(BURNER_PURGE,10);uint32_t purge=now;
 assert(BoardScenario_FanCompare()==0&&!b->out.gas);
 assert(BoardScenario_Start(BURNER_PLC,now)==BURNER_COMMAND_BUSY);
 Until(BURNER_IGNITION_1,31000);assert(now-purge>=30000U&&b->out.gas&&b->out.ignition);
 if(!SIM_FLAME) {
  Until(BURNER_LOCKOUT,41000);assert(b->fault==BURNER_IGNITION_FAILED&&b->attempt==2);
  assert(!b->out.gas&&!b->out.ignition);
  assert(BoardScenario_Start(BURNER_HMI,now)==BURNER_COMMAND_BUSY);return;
 }
 Until(BURNER_RUNNING,5000);assert(b->out.gas&&!b->out.ignition&&b->out.damper==DAMPER_BYPASS);
 if(!SIM_CO) {Until(BURNER_LOCKOUT,100000);assert(b->fault==BURNER_CO_TIMEOUT&&!b->out.distribution_fan);return;}
 for(unsigned n=0;n<30000&&b->sample_state!=SAMPLE_ACCEPTED;n++)BoardScenario_Process(++now);
 assert(b->sample_state==SAMPLE_ACCEPTED&&b->out.damper==DAMPER_PROCESS);
 BoardScenario_Stop(BURNER_HMI,now);assert(b->state==BURNER_SHUTDOWN&&!b->out.gas&&!b->out.ignition);
 Until(BURNER_POSTPURGE,10);uint32_t post=now;
 BoardScenario_Stop(BURNER_HMI,now);assert(b->state_ms==post);
 Until(BURNER_IDLE,61000);assert(now-post>=60000U&&BoardScenario_FanCompare()==1000);
 assert(!physical.commissioned); /* no change to the physical profile */
}
int main(void)
{
 Test(0);Test(UINT32_MAX-10000U);
 puts("PASS on-board DRY RUN model: configured timing, real-time sequence, STOP60s, no flame/CO branches and tick wrap; no HAL/GPIO linked");
}
