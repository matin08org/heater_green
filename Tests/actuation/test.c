#include "BoardActuation.h"
#include "Board595Map.h"
#include "DamperStepper.h"
#include "tim.h"
#include <assert.h>
#include <stdio.h>
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
uint32_t HAL_GetTick(void){return now;}
GPIO_PinState HAL_GPIO_ReadPin(void *p,uint16_t pin){assert(p==HAL1_OPEN_GPIO_Port&&pin==4096);return hall?GPIO_PIN_RESET:GPIO_PIN_SET;}
HAL_StatusTypeDef HC595_ClearAll(void){shadow&=1;return HAL_OK;}
HAL_StatusTypeDef HC595_WriteByte(uint8_t b){shadow=(b&0xfe)|(shadow&1);return HAL_OK;}
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
 assert(!BoardActuation_ConfirmExhaust());
 BurnerOutputs o={.gas=true,.ignition=true,.combustion_fan=true,.exchanger_fan=true,
 .sample_valve_1=true,.sample_valve_2=true,.cooler_fan=true,.distribution_fan=true,
 .air_percent=50,.damper=DAMPER_BYPASS};
 BoardActuation_Apply(&o,true,now);assert(shadow==1&&Snapshot().inhibited);
 Reference();Edge();Edge();Edge();assert(Snapshot().fan_sync);
 BoardActuation_Apply(&o,true,now);assert(compare==180&&shadow==0xdf); /* Q0 retained; distribution blocked */
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
 assert(BoardActuation_ManualStep(-16)==HAL_OK&&!Snapshot().referenced);
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
 puts("PASS actuation: process PWM ownership, synchronized preload/off, shared Q0, all relay mappings, endpoint reference/debounce, bidirectional motion, timeout, manual invalidation and missing-sync inhibit");
}
