#include "CombustionCycle.h"
#include <limits.h>
static void Enter(BurnerControl *b,BurnerSampleState s,uint32_t now)
{b->sample_state=s;b->sample_ms=now;}
static bool Elapsed(uint32_t now,uint32_t start,uint32_t duration)
{return (uint32_t)(now-start)>=duration;}
static void CloseSamples(BurnerControl *b)
{b->out.sample_valve_1=b->out.sample_valve_2=b->out.cooler_fan=false;}
static BurnerFault Adjust(BurnerControl *b,int direction,uint16_t co,uint32_t now)
{
    const BurnerConfig *c=&b->config;
    if(b->air_adjustments>=c->max_adjustments)return BURNER_ADJUST_LIMIT;
    int target=(int)b->out.air_percent+direction*(int)c->air_step_percent;
    if(target<c->air_min_percent||target>c->air_max_percent)return BURNER_ADJUST_LIMIT;
    if(direction>0 && b->have_previous_adjust_co && co>=b->previous_adjust_co)return BURNER_ADJUST_LIMIT;
    b->previous_adjust_co=co;b->have_previous_adjust_co=direction>0;
    ++b->air_adjustments;b->out.air_percent=(uint8_t)target;
    b->out.damper=DAMPER_BYPASS;b->out.distribution_fan=false;CloseSamples(b);
    Enter(b,SAMPLE_AIR_SETTLE,now);return BURNER_NO_FAULT;
}
BurnerFault CombustionCycle_Process(BurnerControl *b,const BurnerInputs *in,uint32_t now)
{
    const BurnerConfig *c=&b->config;
    if(!in->flame_temp_valid)return BURNER_INPUT_STALE;
    if(in->flame_temp_q4>4000)return BURNER_TEMPERATURE;
    bool temp_ok=in->flame_temp_valid&&in->flame_temp_q4>=2800&&in->flame_temp_q4<=4000;
    if(b->sample_state==SAMPLE_IDLE) {
        CloseSamples(b);b->out.damper=DAMPER_BYPASS;b->out.distribution_fan=false;
        b->sample_cycle_ms=now;Enter(b,SAMPLE_WAIT_BYPASS,now);
    }
    if(b->sample_state==SAMPLE_ACCEPTED) {
        bool co_fresh=in->co_valid&&!Elapsed(now,in->co_sample_ms,c->input_max_age_ms);
        if(co_fresh&&in->co_ppm>=c->co_trip_ppm)return BURNER_CO_HIGH;
        if(!temp_ok || !co_fresh || in->co_ppm>c->co_limit_ppm || Elapsed(now,b->sample_ms,c->sample_period_ms)) {
            CloseSamples(b);b->out.damper=DAMPER_BYPASS;b->out.distribution_fan=false;
            b->air_adjustments=0;b->sample_attempts=0;b->have_previous_adjust_co=false;b->sample_cycle_ms=now;
            Enter(b,SAMPLE_WAIT_BYPASS,now);
        } else {
            if(!in->process_proven&&Elapsed(now,b->sample_ms,c->damper_timeout_ms))return BURNER_INTERLOCK;
            b->out.distribution_fan=in->process_proven;return BURNER_NO_FAULT;
        }
    }
    switch(b->sample_state) {
    case SAMPLE_WAIT_BYPASS:
        if(Elapsed(now,b->sample_ms,c->damper_timeout_ms))return BURNER_INTERLOCK;
        if(in->bypass_proven)Enter(b,SAMPLE_WAIT_TEMPERATURE,now);
        break;
    case SAMPLE_WAIT_TEMPERATURE:
        if(in->flame_temp_valid && Elapsed(now,b->sample_cycle_ms,c->initial_measure_delay_ms)) {
            if(b->sample_attempts>c->max_adjustments)return BURNER_ADJUST_LIMIT;
            ++b->sample_attempts;
            b->out.sample_valve_1=true;Enter(b,SAMPLE_VALVE_1,now);
        }
        break;
    case SAMPLE_VALVE_1:
        if(Elapsed(now,b->sample_ms,c->sample_fill_ms)) {
            b->out.sample_valve_1=false;b->out.cooler_fan=true;Enter(b,SAMPLE_COOLING,now);
        }
        break;
    case SAMPLE_COOLING:
        if(Elapsed(now,b->sample_ms,c->cooling_timeout_ms))return BURNER_COOLING_TIMEOUT;
        if(in->cooler_temp_valid && in->cooler_temp_q4<160) {
            b->out.cooler_fan=false;b->out.sample_valve_2=true;Enter(b,SAMPLE_VALVE_2,now);
        }
        break;
    case SAMPLE_VALVE_2:
        if(Elapsed(now,b->sample_ms,c->sample_transfer_ms)) {
            b->out.sample_valve_2=false;Enter(b,SAMPLE_WAIT_CO,now);
        }
        break;
    case SAMPLE_WAIT_CO:
        if(Elapsed(now,b->sample_ms,c->co_timeout_ms))return BURNER_CO_TIMEOUT;
        if(!Elapsed(now,b->sample_ms,c->co_min_wait_ms)||!in->co_valid||
           (uint32_t)(in->co_sample_ms-b->sample_ms)>=INT32_MAX||
           Elapsed(now,in->co_sample_ms,c->input_max_age_ms))break;
        if(in->co_ppm>=c->co_trip_ppm)return BURNER_CO_HIGH;
        if(in->co_ppm>c->co_limit_ppm) {
            if(!in->flame_temp_valid||in->flame_temp_q4<2800)return BURNER_TEMPERATURE;
            return Adjust(b,1,in->co_ppm,now);
        }
        if(!in->flame_temp_valid)return BURNER_INPUT_STALE;
        if(in->flame_temp_q4<2800) {
            if(in->co_ppm>c->co_accept_ppm)return BURNER_TEMPERATURE;
            return Adjust(b,-1,in->co_ppm,now);
        }
        if(in->flame_temp_q4>4000)return BURNER_TEMPERATURE;
        if(in->co_ppm>c->co_accept_ppm) {b->sample_cycle_ms=now;Enter(b,SAMPLE_WAIT_BYPASS,now);break;}
        b->out.damper=DAMPER_PROCESS;b->out.distribution_fan=false;
        Enter(b,SAMPLE_ACCEPTED,now);
        break;
    case SAMPLE_AIR_SETTLE:
        if(Elapsed(now,b->sample_ms,c->air_settle_ms)) {b->sample_cycle_ms=now;Enter(b,SAMPLE_WAIT_BYPASS,now);}
        break;
    default:break;
    }
    return BURNER_NO_FAULT;
}
