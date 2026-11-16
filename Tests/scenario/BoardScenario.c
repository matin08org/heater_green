#include "BoardScenario.h"
#include <string.h>
static BurnerControl model;
static BurnerInputs fixture;
static BurnerCommandResult start_result;
static bool initialized;
static void Inputs(uint32_t now)
{
 /* Explicitly synthetic feedback for the PRIVATE model only. Never published
  * as actual physical proof and never used to authorize BoardActuation. */
 fixture.captured_ms=now;fixture.interlocks_ok=fixture.detector_healthy=true;
 fixture.airflow_proof_valid=fixture.ventilation_safe=true;
 fixture.fuel_off_proven=!model.out.gas;fixture.ignition_off_proven=!model.out.ignition;
 fixture.fan_stopped_proven=!model.out.combustion_fan;
 fixture.airflow=model.out.combustion_fan;
 bool ign=model.state==BURNER_IGNITION_1||model.state==BURNER_IGNITION_2;
 fixture.flame=board_scenario_config.simulate_flame&&
    ((ign&&now-model.state_ms>=board_scenario_config.flame_appears_ms)||model.state==BURNER_RUNNING);
 fixture.flame_temp_valid=true;fixture.flame_temp_q4=3200;
 fixture.cooler_temp_valid=true;
 fixture.cooler_temp_q4=model.sample_state==SAMPLE_COOLING&&now-model.sample_ms>=2000U?156:180;
 fixture.bypass_proven=model.out.damper==DAMPER_BYPASS||model.out.damper==DAMPER_HOLD;
 fixture.process_proven=model.out.damper==DAMPER_PROCESS;
 fixture.co_valid=board_scenario_config.simulate_co&&
    ((model.sample_state==SAMPLE_WAIT_CO&&now-model.sample_ms>=model.config.co_min_wait_ms)||model.sample_state==SAMPLE_ACCEPTED);
 fixture.co_ppm=4;if(fixture.co_valid)fixture.co_sample_ms=now;
 fixture.co2_valid=fixture.daylight_valid=fixture.daylight=true;fixture.co2_ppm=500;
}
void BoardScenario_Init(const BurnerConfig *profile,uint32_t now)
{
 /* Test-only configuration. This is deliberately not board_burner_profile:
  * commissioning/measurements of the PHYSICAL controller remain unchanged. */
 BurnerConfig c={.commissioned=true,.adaptive_air_enabled=true,.cooling_mode=COOL_BY_TEMPERATURE,
 .purge_ms=30000,.required_purge_ms=30000,.interpurge_ms=30000,.postpurge_ms=60000,
 .ignition_ms=5000,.flame_stable_ms=500,.airflow_timeout_ms=5000,.input_max_age_ms=500,
 .co_min_wait_ms=1000,.co_timeout_ms=60000,.co_accept_ppm=5,.co_limit_ppm=10,.co_trip_ppm=50,
 .co2_hysteresis_ppm=100,.max_attempts=2,.fault_postpurge_permitted=true,
 .initial_measure_delay_ms=1000,.sample_period_ms=300000,.sample_fill_ms=10000,.sample_transfer_ms=10000,
 .cooling_timeout_ms=180000,.air_settle_ms=30000,.damper_timeout_ms=15000,
 .air_initial_percent=100,.air_min_percent=10,.air_max_percent=100,.air_step_percent=10,.max_adjustments=3};
 if(profile) {
  c.purge_ms=profile->purge_ms;c.required_purge_ms=profile->purge_ms;
  c.interpurge_ms=profile->interpurge_ms;c.postpurge_ms=profile->postpurge_ms;
  c.ignition_ms=profile->ignition_ms;c.flame_stable_ms=profile->flame_stable_ms;
 }
 initialized=board_scenario_config.enabled&&Burner_ConfigValid(&c)&&
    board_scenario_config.fan_on_ccr<=333U&&board_scenario_config.fan_off_ccr==1000U&&
    board_scenario_config.flame_appears_ms+c.flame_stable_ms<c.ignition_ms;
 memset(&fixture,0,sizeof(fixture));Burner_Init(&model,&c,now);Inputs(now);
 start_result=BURNER_COMMAND_CONFIG_REQUIRED;
}
BurnerControl *BoardScenario_Control(void){return &model;}
BurnerCommandResult BoardScenario_Start(BurnerSource source,uint32_t now)
{
 if(!initialized)return BURNER_COMMAND_CONFIG_REQUIRED;
 Inputs(now);start_result=Burner_RequestStart(&model,source,&fixture,now);return start_result;
}
void BoardScenario_Stop(BurnerSource source,uint32_t now){Burner_RequestStop(&model,source,now);}
void BoardScenario_Process(uint32_t now)
{if(initialized){Inputs(now);Burner_Process(&model,&fixture,now);}}
void BoardScenario_ReportOutputFault(uint32_t now)
{Inputs(now);Burner_ReportActuationFault(&model,&fixture,now);}
bool BoardScenario_Acknowledge(uint32_t now)
{Inputs(now);return initialized&&Burner_AcknowledgeFault(&model,&fixture,now);}
uint16_t BoardScenario_FanCompare(void)
{return model.out.combustion_fan?board_scenario_config.fan_on_ccr:board_scenario_config.fan_off_ccr;}
