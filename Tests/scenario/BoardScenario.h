#ifndef BOARD_SCENARIO_H
#define BOARD_SCENARIO_H
#include "BurnerControl.h"
/* Main-context, non-energizing scenario. This module never links/calls HAL,
 * GPIO, fan PWM, relays, SPI or stepper APIs. Physical input data is untouched. */
typedef struct {
 bool enabled,simulate_flame,simulate_co;
 uint16_t fan_on_ccr,fan_off_ccr;
 uint32_t flame_appears_ms;
} BoardScenarioConfig;
extern const BoardScenarioConfig board_scenario_config;
void BoardScenario_Init(const BurnerConfig *profile,uint32_t now);
BurnerControl *BoardScenario_Control(void);
BurnerCommandResult BoardScenario_Start(BurnerSource source,uint32_t now);
void BoardScenario_Stop(BurnerSource source,uint32_t now);
void BoardScenario_Process(uint32_t now);
void BoardScenario_ReportOutputFault(uint32_t now);
bool BoardScenario_Acknowledge(uint32_t now);
uint16_t BoardScenario_FanCompare(void);
#endif
