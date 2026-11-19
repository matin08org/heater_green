#include "BoardActuation.h"
#include "Board595Map.h"
#include "DamperStepper.h"
#include "tim.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
MockTimer mock_timer;
const BoardActuationConfig board_actuation_config={
 .fan_calibrated=true,.fan_ccr={1000,300,270,240,210,180,150,120,90,60,0},
 .travel_pulses=1600,.travel_timeout_ms=15000,.hall_debounce_ms=20,.endpoint_tolerance_pulses=32,.rpm=30};
static uint32_t now;
static bool hall,running;
static int32_t position,endpoint;
static uint8_t shadow=1;
static uint16_t compare;
static unsigned starts;
static uint32_t bus_errors;
uint32_t HAL_GetTick(void){return now;}
GPIO_PinState HAL_GPIO_ReadPin(void *p,uint16_t pin){assert(p==HAL1_OPEN_GPIO_Port&&pin==4096);return hall?GPIO_PIN_RESET:GPIO_PIN_SET;}
HAL_StatusTypeDef HC595_ClearAll(void){shadow&=1;return HAL_OK;}
HAL_StatusTypeDef HC595_WriteByte(uint8_t b){shadow=(b&0xfe)|(shadow&1);return HAL_OK;}
void HC595_GetDiagnostics(HC595_Diagnostics *d){memset(d,0,sizeof(*d));d->initialized=1;d->shadow=d->latched=shadow;d->errors=bus_errors;}
HAL_StatusTypeDef TIM1_PhaseSetCompare(uint16_t c){assert(c<=1000);compare=c;return HAL_OK;}
void DamperStepper_Stop(void){running=false;}
uint8_t DamperStepper_IsRunning(void){return running;}
int32_t DamperStepper_GetPosition(void){return position;}
HAL_StatusTypeDef DamperStepper_SetSpeedRPM(float rpm){assert(rpm==30);return HAL_OK;}
HAL_StatusTypeDef DamperStepper_SetEndpoint(int32_t p,DamperStepper_EndAction_t a){assert(!running&&a==DAMPER_END_STOP);endpoint=p;return HAL_OK;}
HAL_StatusTypeDef DamperStepper_Start(void){running=true;starts++;return HAL_OK;}
static void Service(uint32_t dt){now+=dt;BoardActuation_Service(now);mock_timer.SR=0;}
static void Edge(void){mock_timer.SR=TIM_SR_CC2IF;mock_timer.CCR2=333;Service(10);}
static BoardActuationSnapshot Snapshot(void){BoardActuationSnapshot s;BoardActuation_Get(&s);return s;}
static void Reference(void){hall=true;Service(1);Service(20);assert(BoardActuation_ConfirmExhaust());}
int main(void)
{
 BoardActuation_Init();assert(compare==1000&&!(mock_timer.BDTR&TIM_BDTR_MOE));
 assert(BoardActuation_ConfirmExhaust()); /* PB12 HIGH: operator reference allowed */
 Service(100);assert(Snapshot().referenced&&Snapshot().bypass_proven&&!Snapshot().fault);
 running=true;assert(!BoardActuation_ConfirmExhaust());running=false;
 BoardActuation_Abort();assert(!Snapshot().referenced);
 Reference();hall=false;Service(1);Service(20);
 assert(Snapshot().fault&&!Snapshot().referenced); /* Hall-backed reference loss retained */
 BoardActuation_Init();
 BurnerOutputs o={.gas=true,.ignition=true,.combustion_fan=true,.exchanger_fan=true,
 .sample_valve_1=true,.sample_valve_2=true,.cooler_fan=true,.distribution_fan=true,
 .air_percent=50,.damper=DAMPER_BYPASS};
 BoardActuation_Apply(&o,true,now);assert(shadow==1&&Snapshot().inhibited);
 assert(Snapshot().inhibit_flags==(ACT_BLOCK_REFERENCE|ACT_BLOCK_SYNC));
 assert(!strcmp(BoardActuation_InhibitText(Snapshot().inhibit_flags),"EXHAUST reference not confirmed"));
 assert(!strcmp(BoardActuation_InhibitText(ACT_BLOCK_FAULT|ACT_BLOCK_REFERENCE),"Actuator fault; inspect damper"));
 Reference();Edge();Edge();Edge();assert(Snapshot().fan_sync);
 BoardActuation_Apply(&o,true,now);assert(compare==180&&shadow==0xdf); /* Q0 retained; distribution blocked */
 assert(Snapshot().inhibit_flags==0);
 assert(!(mock_timer.BDTR&TIM_BDTR_MOE));Edge();assert(!(mock_timer.BDTR&TIM_BDTR_MOE));
 Edge();assert(mock_timer.BDTR&TIM_BDTR_MOE);
 o.damper=DAMPER_PROCESS;BoardActuation_Apply(&o,true,now);
 assert(starts==1&&endpoint==-1600&&Snapshot().moving&&!Snapshot().bypass_proven);
 BoardActuation_Apply(&o,true,now);assert(starts==1); /* command is idempotent */
 hall=false;Service(1);Service(20);position=-1600;running=false;hall=true;
 Service(1);assert(Snapshot().moving);Service(20);assert(Snapshot().process_proven);
 Edge();Edge();Edge();BoardActuation_Apply(&o,true,now);assert(shadow==0xff);
 o.damper=DAMPER_BYPASS;BoardActuation_Apply(&o,true,now);assert(endpoint==0&&running);
 hall=false;Service(1);Service(20);running=false;position=0;hall=true;Service(1);Service(20);
 assert(Snapshot().bypass_proven&&!Snapshot().process_proven);
 Edge();Edge();Edge();BoardActuation_Apply(&o,false,now);
 assert(shadow==1&&compare==1000&&!(mock_timer.BDTR&TIM_BDTR_MOE));
 assert(Snapshot().inhibit_flags==ACT_BLOCK_PERMISSION);
 assert(BoardActuation_ManualStep(-16)==HAL_OK&&!Snapshot().referenced);
 /* Idle runtime polling must not abort a manual move while proof is unknown. */
 BurnerOutputs idle={0};BoardActuation_Apply(&idle,false,now);
 assert(running&&!Snapshot().referenced);
 running=false;Reference();Edge();Edge();Edge();
 o.damper=DAMPER_PROCESS;BoardActuation_Apply(&o,true,now);assert(running);
 Service(15000);assert(Snapshot().fault&&!Snapshot().referenced&&!running);
 BoardActuation_Apply(&o,true,now);assert(shadow==1);
 Reference();Edge();Edge();Edge();o.damper=DAMPER_PROCESS;
 BoardActuation_Apply(&o,true,now);hall=false;Service(1);Service(20);
 position=endpoint+16;hall=true;Service(1);Service(20);
 assert(!running&&Snapshot().process_proven&&!Snapshot().fault); /* magnet stops motion near target */
 Edge();Edge();Edge();o.damper=DAMPER_BYPASS;BoardActuation_Apply(&o,true,now);
 hall=false;Service(1);Service(20);position=endpoint-100;hall=true;Service(1);Service(20);
 assert(!running&&Snapshot().fault); /* unexpected early magnet cannot prove destination */
 Reference();Edge();Edge();Edge();o.damper=DAMPER_BYPASS;o.air_percent=55;
 BoardActuation_Apply(&o,true,now);assert(Snapshot().fault&&compare==1000&&shadow==1);
 Reference();Edge();Edge();Edge();o.air_percent=50;BoardActuation_Apply(&o,true,now);
 Edge();Edge();Service(25);assert(!Snapshot().fan_sync&&!(mock_timer.BDTR&TIM_BDTR_MOE));
 Edge();assert(!Snapshot().fan_sync);Edge();assert(!Snapshot().fan_sync);
 Edge();assert(Snapshot().fan_sync); /* stale sync cannot recover on one pulse */
 Service(25);
 BoardActuation_Apply(&o,true,now);assert(shadow==1);
 BoardActuation_Init();running=false;
 assert(!BoardActuation_StartRelayTest(false,now));
 assert(BoardActuation_StartRelayTest(true,now)&&shadow==3);
 assert(BoardActuation_ManualStep(160)==HAL_BUSY);
 for(unsigned step=1;step<=7;step++) {
     uint8_t expected=(uint8_t)(((1U<<(step+1U))-2U)|1U);
     BoardActuation_Apply(&idle,false,now);assert(shadow==expected);
     Service(2999);assert(shadow==expected);
     Service(1);
 }
 assert(shadow==1&&!BoardActuation_RelayTestActive());
 assert(BoardActuation_StartRelayTest(true,now));
 BoardActuation_StopRelayTest();assert(shadow==1);
 now=UINT32_MAX-1000U;assert(BoardActuation_StartRelayTest(true,now));
 Service(3000);assert(shadow==7); /* unsigned tick wrap */
 Service(6000);assert(shadow==1&&!BoardActuation_RelayTestActive());
 assert(BoardActuation_StartRelayTest(true,now));
 BoardActuation_Apply(&o,false,now);assert(!BoardActuation_RelayTestActive()&&shadow==1);
 BoardActuation_Init();hall=false;running=false;shadow=1;
 assert(!BoardActuation_TuneFan(200,true,now)); /* no sync */
 Edge();Edge();Edge();assert(Snapshot().fan_sync);
 assert(!BoardActuation_TuneFan(200,false,now));
 assert(BoardActuation_TuneFan(400,true,now)&&Snapshot().fan_tuning&&compare==400);
 BoardActuation_Apply(&idle,false,now);assert(compare==400&&shadow==1);
 Edge();Edge();assert(mock_timer.BDTR&TIM_BDTR_MOE);
 assert(!BoardActuation_StartRelayTest(true,now));
 assert(BoardActuation_ManualStep(160)==HAL_BUSY);
 assert(BoardActuation_TuneFan(450,true,now)&&compare==450);
 assert(BoardActuation_TuneFan(1000,true,now)&&!Snapshot().fan_tuning&&compare==1000);
 assert(!(mock_timer.BDTR&TIM_BDTR_MOE));
 assert(BoardActuation_TuneFan(0,true,now));
 shadow=3;Service(1);assert(!Snapshot().fan_tuning&&compare==1000);shadow=1;
 assert(BoardActuation_TuneFan(100,true,now));
 ++bus_errors;Service(1);assert(!Snapshot().fan_tuning);
 assert(BoardActuation_TuneFan(150,true,now));
 Service(25);assert(!Snapshot().fan_tuning&&!(mock_timer.BDTR&TIM_BDTR_MOE));
 Edge();Edge();Edge();assert(BoardActuation_TuneFan(200,true,now));
 BoardActuation_Apply(&o,false,now);assert(!Snapshot().fan_tuning&&shadow==1);
 Edge();Edge();Edge();assert(BoardActuation_TuneFan(250,true,now));
 for(unsigned i=0;i<12000;i++)Edge();
 assert(!Snapshot().fan_tuning&&compare==1000); /* applies never extend session */
 assert(!BoardActuation_TuneFan(1001,true,now));
 puts("PASS fan tuning: unreferenced idle fan-only ownership, CCR range, preload, OFF, relay/motor exclusion, bus/sync loss, process takeover and timeout");
 puts("PASS service relay sequence: Q1..Q7 every 3s, Q0 retained, all OFF at 21s, idle ownership, cancellation, stall timeout and tick wrap");
 puts("PASS actuation: process PWM ownership, synchronized preload/off, shared Q0, all relay mappings, endpoint reference/debounce, bidirectional motion, timeout, manual invalidation and missing-sync inhibit");
}
