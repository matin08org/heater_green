#include "BoardOutputTest.h"
#include "Board595Map.h"
#include "DamperStepper.h"
#include "tim.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
MockTimer mock_timer;
const BoardActuationConfig board_actuation_config={.fan_calibrated=false,
 .fan_ccr={1000,1000,1000,1000,1000,1000,1000,1000,1000,1000,1000},
 .travel_pulses=1600,.travel_timeout_ms=15000,.hall_debounce_ms=20,.endpoint_tolerance_pulses=32,.rpm=30};
const BoardOutputTestConfig board_output_test_config={.enabled=true,.require_dummy_load_confirmation=true,
 .fan_on_ccr=0,.arm_timeout_ms=30000,.max_run_ms=90000,.max_total_ms=155000};
static uint32_t now,errors;static bool hall,running;static uint8_t shadow=1,latched=1;
static int32_t position,endpoint;static uint16_t compare;
uint32_t HAL_GetTick(void){return now;}
GPIO_PinState HAL_GPIO_ReadPin(void *p,uint16_t pin){assert(p==HAL1_OPEN_GPIO_Port&&pin==4096);return hall?GPIO_PIN_RESET:GPIO_PIN_SET;}
HAL_StatusTypeDef HC595_ClearAll(void){shadow&=1;latched=shadow;return HAL_OK;}
HAL_StatusTypeDef HC595_WriteByte(uint8_t v){shadow=(v&0xfe)|(shadow&1);latched=shadow;return HAL_OK;}
void HC595_GetDiagnostics(HC595_Diagnostics *d){memset(d,0,sizeof(*d));d->initialized=1;d->shadow=shadow;d->latched=latched;d->errors=errors;}
HAL_StatusTypeDef TIM1_PhaseSetCompare(uint16_t c){assert(c<=1000);compare=c;return HAL_OK;}
void DamperStepper_Stop(void){running=false;}
uint8_t DamperStepper_IsRunning(void){return running;}
int32_t DamperStepper_GetPosition(void){return position;}
HAL_StatusTypeDef DamperStepper_SetSpeedRPM(float rpm){assert(rpm==30);return HAL_OK;}
HAL_StatusTypeDef DamperStepper_SetEndpoint(int32_t p,DamperStepper_EndAction_t a){assert(!running&&a==DAMPER_END_STOP);endpoint=p;return HAL_OK;}
HAL_StatusTypeDef DamperStepper_Start(void){running=true;return HAL_OK;}
static void Service(unsigned dt){now+=dt;BoardActuation_Service(now);mock_timer.SR=0;}
static void Edge(void){mock_timer.SR=TIM_SR_CC2IF;mock_timer.CCR2=333;Service(10);}
static BoardOutputTestSnapshot Snapshot(void){BoardOutputTestSnapshot s;BoardOutputTest_Get(&s);return s;}
static BurnerOutputs Out(void){return (BurnerOutputs){.combustion_fan=true,.exchanger_fan=true,.air_percent=100,.damper=DAMPER_BYPASS};}
static void Ready(void)
{
 now=errors=0;hall=running=false;position=endpoint=0;shadow=latched=1;memset(&mock_timer,0,sizeof(mock_timer));
 BoardActuation_Init();BoardOutputTest_Init();hall=true;Service(1);Service(20);
 assert(BoardActuation_ConfirmExhaust());Edge();Edge();Edge();
 assert(BoardOutputTest_Arm(true,now));assert(shadow==1&&compare==1000&&!Snapshot().active);
}
static void AllOff(void){assert(shadow==1&&latched==1&&compare==1000&&!(mock_timer.BDTR&TIM_BDTR_MOE)&&!running);}
int main(void)
{
 BoardActuation_Init();BoardOutputTest_Init();BurnerOutputs o=Out();o.gas=o.ignition=true;
 assert(!BoardOutputTest_Process(&o,BURNER_IGNITION_1,now));AllOff();
 assert(!BoardOutputTest_Arm(false,now));AllOff();
 Ready();assert(!BoardOutputTest_Process(&o,BURNER_IDLE,now));AllOff();
 o=Out();assert(!BoardOutputTest_Process(&o,BURNER_PRECHECK,now));assert(shadow==5&&compare==0);
 Edge();Edge();assert(mock_timer.BDTR&TIM_BDTR_MOE);
 o.gas=o.ignition=true;assert(!BoardOutputTest_Process(&o,BURNER_IGNITION_1,now));assert((shadow&0x82)==0x82);
 o.ignition=false;o.sample_valve_1=true;
 assert(!BoardOutputTest_Process(&o,BURNER_RUNNING,now));assert((shadow&0x8a)==0x88);
 o.gas=false;o.sample_valve_1=false;assert(!BoardOutputTest_Process(&o,BURNER_SHUTDOWN,now));
 assert(!(shadow&0x82)&&compare==0); /* immediate logical off; fan retained */
 assert(!BoardOutputTest_Process(&o,BURNER_POSTPURGE,now));
 o=(BurnerOutputs){0};assert(!BoardOutputTest_Process(&o,BURNER_IDLE,now));AllOff();assert(!Snapshot().armed);
 Ready();o=Out();assert(!BoardOutputTest_Process(&o,BURNER_PRECHECK,now));
 uint32_t begin=now;now=begin+89970;Edge();Edge();Edge();o.gas=true;
 assert(BoardOutputTest_Process(&o,BURNER_RUNNING,now));assert(!(shadow&0x82)&&Snapshot().stop_requested);
 now=begin+155000;assert(BoardOutputTest_Process(&o,BURNER_POSTPURGE,now));
 assert(Snapshot().reason==OUTPUT_TEST_TOTAL_LIMIT);AllOff();
 Ready();o=Out();Service(25);assert(BoardOutputTest_Process(&o,BURNER_PRECHECK,now));
 assert(Snapshot().reason==OUTPUT_TEST_SYNC_LOST);AllOff();
 Ready();o=Out();assert(!BoardOutputTest_Process(&o,BURNER_PRECHECK,now));++errors;
 assert(BoardOutputTest_Process(&o,BURNER_IGNITION_1,now));assert(Snapshot().reason==OUTPUT_TEST_SPI_ERROR);AllOff();
 Ready();now+=30000;assert(BoardOutputTest_Process(&o,BURNER_IDLE,now));
 assert(Snapshot().reason==OUTPUT_TEST_ARM_EXPIRED);AllOff();
 Ready();o=Out();o.gas=true;assert(BoardOutputTest_Process(&o,BURNER_PURGE,now));
 assert(Snapshot().reason==OUTPUT_TEST_INVALID_REQUEST);AllOff();
 Ready();o=Out();assert(!BoardOutputTest_Process(&o,BURNER_PRECHECK,now));
 o.sample_valve_1=o.sample_valve_2=true;assert(BoardOutputTest_Process(&o,BURNER_RUNNING,now));AllOff();
 Ready();o=Out();assert(!BoardOutputTest_Process(&o,BURNER_PRECHECK,now));o.gas=o.ignition=true;
 assert(!BoardOutputTest_Process(&o,BURNER_IGNITION_1,now));BoardOutputTest_Disarm();AllOff();
 assert(!Snapshot().armed&&!Snapshot().operator_confirmed_dummy_loads);
 Ready();o=Out();assert(!BoardOutputTest_Process(&o,BURNER_PRECHECK,now));o.damper=DAMPER_PROCESS;
 assert(!BoardOutputTest_Process(&o,BURNER_RUNNING,now)&&running&&endpoint==-1600);
 BoardOutputTest_Disarm();AllOff();
 puts("PASS dummy-load output test: default OFF, confirmation, real adapter mapping, Q0, synchronized PWM, STOP, expiry/run/total limits, SPI/sync/motion guards and disarm");
}
