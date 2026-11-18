#include "BurnerControl.h"
#include "StartOverride.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Same numbers as Assets/board.json: unset (null) entries are zero. */
static BurnerConfig Profile(void)
{
    return (BurnerConfig){.commissioned=false,.adaptive_air_enabled=true,
        .cooling_mode=COOL_BY_TEMPERATURE,.purge_ms=30000,.interpurge_ms=30000,
        .postpurge_ms=60000,.ignition_ms=5000,.flame_stable_ms=500,.input_max_age_ms=500,
        .max_attempts=2,.sample_period_ms=300000,.sample_fill_ms=10000,
        .sample_transfer_ms=10000,.damper_timeout_ms=15000,.air_min_percent=10,
        .air_max_percent=100,.air_step_percent=10,.co2_hysteresis_ppm=100};
}
/* Rig: real inputs as this board delivers them. No proof wiring, no CO. */
typedef struct {
    BurnerControl b;BurnerInputs real,eff;uint32_t now;
    bool fan_sync,flame_forced,flame_blocked,feed;uint32_t gas_since;bool gas_seen;
} Rig;
static void Step(Rig *r)
{
    r->now+=100U;
    if(r->feed)r->real.captured_ms=r->now;
    if(r->b.out.gas&&r->b.out.ignition){if(!r->gas_seen){r->gas_seen=true;r->gas_since=r->now;}}
    else if(!r->b.out.gas)r->gas_seen=false;
    if(!r->flame_forced)r->real.flame=!r->flame_blocked&&r->gas_seen&&r->b.out.gas&&
        (uint32_t)(r->now-r->gas_since)>=700U;
    StartOverride_Inputs(&r->real,&r->b.out,r->fan_sync,false,&r->eff);
    Burner_Process(&r->b,&r->eff,r->now);
}
static void Run(Rig *r,uint32_t ms){for(uint32_t t=0;t<ms;t+=100U)Step(r);}
static Rig NewRig(BurnerConfig *effective_out)
{
    BurnerConfig profile=Profile(),eff;(void)StartOverride_Config(&profile,&eff);
    Rig r;memset(&r,0,sizeof(r));
    Burner_Init(&r.b,&eff,0);if(effective_out)*effective_out=eff;
    r.real=(BurnerInputs){.flame_temp_valid=true,.flame_temp_q4=3200,
        .cooler_temp_valid=true,.cooler_temp_q4=120};
    r.fan_sync=true;r.feed=true;r.now=1000U;r.real.captured_ms=r.now;
    StartOverride_Inputs(&r.real,&r.b.out,true,false,&r.eff);
    return r;
}
static void Start(Rig *r)
{assert(Burner_RequestStart(&r->b,BURNER_HMI,&r->eff,r->now)==BURNER_COMMAND_ACCEPTED);}

int main(void)
{
    /* 1. The unmodified profile is refused; the override config is accepted. */
    BurnerConfig profile=Profile(),eff;
    assert(!Burner_ConfigValid(&profile));
    uint32_t ph=StartOverride_Config(&profile,&eff);
    assert(Burner_ConfigValid(&eff)&&(ph&SO_PH_COMMISSIONED)&&(ph&SO_PH_CO_LIMIT));
    assert(eff.co_accept_ppm<eff.co_limit_ppm&&eff.co_limit_ppm<eff.co_trip_ppm);
    assert(eff.required_purge_ms==eff.purge_ms);
    /* Owner-supplied values always win over placeholders. */
    profile.co_limit_ppm=300;profile.co_accept_ppm=150;profile.co_trip_ppm=600;
    ph=StartOverride_Config(&profile,&eff);
    assert(eff.co_limit_ppm==300&&eff.co_accept_ppm==150&&eff.co_trip_ppm==600&&!(ph&SO_PH_CO_LIMIT));
    assert(Burner_ConfigValid(&eff));

    /* 2. The warning list still reports the real reasons. */
    {
        Rig r=NewRig(NULL);BurnerControl view=r.b;view.config=Profile();
        BurnerHardwareStatus h={.proof_inputs_connected=false};
        uint32_t m=Burner_StartBlockers(&view,&r.real,&h,r.now);
        assert((m&BURNER_BLOCK_PROOF_CONNECTION)&&(m&BURNER_BLOCK_COMMISSIONING)&&
               (m&BURNER_BLOCK_PARAMETERS)&&(m&BURNER_BLOCK_PROOF_STATE)&&!(m&BURNER_BLOCK_BUSY));
    }

    /* 3. START is accepted and drives gas, ignition and fan for real. */
    {
        Rig r=NewRig(NULL);Start(&r);
        Run(&r,200);assert(r.b.state==BURNER_PURGE&&r.b.out.combustion_fan&&!r.b.out.gas);
        Run(&r,29000);assert(r.b.state==BURNER_PURGE&&!r.b.out.gas);   /* timed purge holds */
        Run(&r,2000);
        assert(r.b.state==BURNER_IGNITION_1&&r.b.out.gas&&r.b.out.ignition&&r.b.out.combustion_fan);
        Run(&r,1500);assert(r.b.state==BURNER_RUNNING&&r.b.out.gas&&!r.b.out.ignition);
        /* 4. Real flame loss still locks out and removes gas. */
        r.flame_forced=true;r.real.flame=false;Run(&r,300);
        assert(r.b.state==BURNER_LOCKOUT&&r.b.fault==BURNER_FLAME_LOST&&!r.b.out.gas&&!r.b.out.ignition);
    }
    /* 5. No flame: gas is cut after 5 s, second attempt, then lockout. */
    {
        Rig r=NewRig(NULL);r.flame_blocked=true;Start(&r);
        Run(&r,31500);assert(r.b.state==BURNER_IGNITION_1&&r.b.out.gas);
        Run(&r,5000);assert(!r.b.out.gas&&!r.b.out.ignition);
        Run(&r,60000);Run(&r,6000);
        assert(r.b.state==BURNER_LOCKOUT&&r.b.fault==BURNER_IGNITION_FAILED&&!r.b.out.gas);
    }
    /* 6. False flame during purge. */
    {
        Rig r=NewRig(NULL);Start(&r);Run(&r,5000);
        r.flame_forced=true;r.real.flame=true;Run(&r,200);
        assert(r.b.state==BURNER_LOCKOUT&&r.b.fault==BURNER_FALSE_FLAME&&!r.b.out.gas);
    }
    /* 7. Stale input data still trips. */
    {
        Rig r=NewRig(NULL);Start(&r);Run(&r,35000);assert(r.b.out.gas);
        r.feed=false;Run(&r,800);
        assert(r.b.state==BURNER_LOCKOUT&&r.b.fault==BURNER_INPUT_STALE&&!r.b.out.gas);
    }
    /* 8. STOP removes gas at once, then post-purge and IDLE. */
    {
        Rig r=NewRig(NULL);Start(&r);Run(&r,35000);assert(r.b.state==BURNER_RUNNING);
        Burner_RequestStop(&r.b,BURNER_HMI,r.now);assert(!r.b.out.gas&&!r.b.out.ignition);
        Run(&r,61000);assert(r.b.state==BURNER_POSTPURGE||r.b.state==BURNER_IDLE);
        Run(&r,500);assert(r.b.state==BURNER_IDLE&&!r.b.out.combustion_fan);
    }
    /* 9. Loss of fan drive sync (the only airflow evidence) stops the purge. */
    {
        Rig r=NewRig(NULL);Start(&r);Run(&r,3000);r.fan_sync=false;Run(&r,6000);
        assert(r.b.state==BURNER_LOCKOUT&&r.b.fault==BURNER_AIRFLOW&&!r.b.out.gas);
    }
    /* 10. Real proof inputs, once wired, are never overridden. */
    {
        BurnerInputs real={.airflow=false},out;BurnerOutputs req={.combustion_fan=true};
        StartOverride_Inputs(&real,&req,true,true,&out);
        assert(!out.airflow&&!out.interlocks_ok&&!out.detector_healthy);
    }
    /* 11. A running cycle is still BUSY; a locked-out one is acknowledged. */
    {
        Rig r=NewRig(NULL);Start(&r);Run(&r,200);
        assert(Burner_RequestStart(&r.b,BURNER_HMI,&r.eff,r.now)==BURNER_COMMAND_BUSY);
        r.flame_forced=true;r.real.flame=true;Run(&r,200);assert(r.b.state==BURNER_LOCKOUT);
        r.real.flame=false;r.flame_forced=true;Run(&r,200);
        assert(Burner_AcknowledgeFault(&r.b,&r.eff,r.now)&&r.b.state==BURNER_IDLE);
        r.flame_forced=false;Start(&r);
    }
    puts("start override tests passed");
    return 0;
}
