#include "KeyInput.h"
#include "UiNavigation.h"
#include "DiagLog.h"
#include "BurnerControl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t now;
static void Advance(KeyInput *k,uint8_t raw,uint32_t ms)
{ for (uint32_t i=0;i<ms;i+=5U) { now+=5U; KeyInput_Update(k,raw,now); } }
static void Armed(KeyInput *k,uint32_t base)
{ now=base; KeyInput_Init(k,now); Advance(k,0,40); assert(k->armed); }
static void Empty(KeyInput *k) { KeyCode e; assert(!KeyInput_Read(k,&e)); }
static void Event(KeyInput *k,KeyCode expected)
{ KeyCode e; assert(KeyInput_Read(k,&e)); assert(e==expected); }
static void Keys(uint32_t base)
{
    KeyInput k; Armed(&k,base);
    for (unsigned i=0;i<10U;i++) { Advance(&k,KEY_MASK(KEY_OK),10); Advance(&k,0,10); }
    Empty(&k); Advance(&k,KEY_MASK(KEY_OK),1000); Empty(&k);
    Advance(&k,0,40); Event(&k,KEY_OK); Empty(&k);
    Advance(&k,KEY_MASK(KEY_CANCEL),40); Empty(&k);
    Advance(&k,0,40); Event(&k,KEY_CANCEL); Empty(&k);
    Advance(&k,KEY_MASK(KEY_UP),600);
    unsigned count=0; KeyCode e;
    while (KeyInput_Read(&k,&e)) { assert(e==KEY_UP); count++; }
    assert(count>=2 && count<=4);
    Advance(&k,0,40);
    uint8_t chord=KEY_MASK(KEY_OK);
    Advance(&k,chord,40); Advance(&k,chord,4990); Empty(&k);
    Advance(&k,chord,10); Event(&k,KEY_DEBUG); Empty(&k);
    Advance(&k,chord,6000); Empty(&k); /* One event per hold. */
    Advance(&k,KEY_MASK(KEY_CANCEL),100); Advance(&k,0,80); Empty(&k);
    Advance(&k,chord|KEY_MASK(KEY_CANCEL),1000); Advance(&k,0,40); Empty(&k);
    Advance(&k,chord,40); now+=6000U; KeyInput_Update(&k,chord,now); Empty(&k);
    assert(k.gaps==1 && !k.armed);
    Advance(&k,chord,6000); Empty(&k); /* Missing samples can't count as a hold. */
    Advance(&k,0,80); assert(k.armed);
    KeyInput_RequireRelease(&k); Advance(&k,KEY_MASK(KEY_OK),6000); Empty(&k);
    Advance(&k,0,80); assert(k.armed);
    Advance(&k,KEY_MASK(KEY_DOWN),8000); assert(k.dropped && k.count==16);
    KeyInput_RequireRelease(&k); Empty(&k);
    now=base; KeyInput_Init(&k,now); Advance(&k,chord,6000); Empty(&k); /* Held at boot. */
    Advance(&k,0,80); assert(k.armed);
}
static void Navigation(void)
{
    UiNavigation n; UiNavigation_Init(&n);
    assert(n.focus==1); /* Safe default is selection STOP, not a stop command. */
    for (unsigned i=0;i<20;i++) {
        assert(UiNavigation_Key(&n,KEY_DOWN,false,800)==NAV_NONE);
        assert(!n.editing && n.page==UI_PAGE_HOME);
    }
    assert(n.focus==1&&UiNavigation_Key(&n,KEY_OK,false,800)==NAV_STOP&&n.page==UI_PAGE_HOME);
    assert(UiNavigation_Key(&n,KEY_UP,false,800)==NAV_NONE&&n.focus==0);
    assert(UiNavigation_Key(&n,KEY_OK,false,800)==NAV_START&&n.page==UI_PAGE_HOME);
    assert(UiNavigation_Key(&n,KEY_DOWN,false,800)==NAV_NONE&&n.focus==1);
    assert(UiNavigation_Key(&n,KEY_OK,false,800)==NAV_STOP);
    assert(UiNavigation_Key(&n,KEY_CANCEL,false,800)==NAV_PAGE&&n.page==UI_PAGE_SETTINGS);
    assert(n.focus==0);
    UiNavigation_Key(&n,KEY_OK,false,800); assert(n.editing);
    UiNavigation_Key(&n,KEY_UP,false,800); assert(n.draft_auto);
    assert(UiNavigation_Key(&n,KEY_CANCEL,false,800)==NAV_NONE && !n.editing);
    UiNavigation_Key(&n,KEY_DOWN,false,800); assert(n.focus==1);
    UiNavigation_Key(&n,KEY_OK,false,800);
    UiNavigation_Key(&n,KEY_UP,false,800); assert(n.draft_co2==810);
    assert(UiNavigation_Key(&n,KEY_OK,false,800)==NAV_SAVE_CO2);
    assert(!n.editing && n.draft_co2==810);
    UiNavigation_Key(&n,KEY_OK,false,5000); UiNavigation_Key(&n,KEY_UP,false,5000);
    assert(n.draft_co2==5000);
    assert(UiNavigation_Key(&n,KEY_DEBUG,false,800)==NAV_PAGE);
    assert(n.page==UI_PAGE_DEBUG && !n.editing);
    assert(UiNavigation_Key(&n,KEY_DEBUG,false,800)==NAV_NONE);
    assert(UiNavigation_Key(&n,KEY_UP,false,800)==NAV_SCROLL_UP);
    assert(UiNavigation_Key(&n,KEY_OK,false,800)==NAV_LIVE);
    assert(UiNavigation_Key(&n,KEY_CANCEL,false,800)==NAV_PAGE && n.page==UI_PAGE_HOME);
    n.page=UI_PAGE_SETTINGS; n.focus=2;
    assert(UiNavigation_Key(&n,KEY_OK,false,800)==NAV_START);
    n.focus=3; assert(UiNavigation_Key(&n,KEY_OK,false,800)==NAV_STOP);
    for(unsigned focus=0;focus<6;focus++) {
        UiNavigation t=n;t.focus=(uint8_t)focus;
        assert(UiNavigation_Key(&t,KEY_OK,false,800)!=NAV_RESET);
    }
    n.focus=4;assert(UiNavigation_Key(&n,KEY_OK,false,800)==NAV_PAGE && n.page==UI_PAGE_SERVICE);
    assert(UiNavigation_Key(&n,KEY_OK,false,800)==NAV_STEP_PLUS);
    UiNavigation_Key(&n,KEY_DOWN,false,800);
    assert(UiNavigation_Key(&n,KEY_OK,false,800)==NAV_STEP_MINUS);
    UiNavigation_Key(&n,KEY_DOWN,false,800);
    assert(UiNavigation_Key(&n,KEY_OK,false,800)==NAV_MOTOR_STOP);
    UiNavigation_Key(&n,KEY_DOWN,false,800);
    assert(UiNavigation_Key(&n,KEY_OK,false,800)==NAV_BEEP);
    UiNavigation_Key(&n,KEY_DOWN,false,800);
    assert(UiNavigation_Key(&n,KEY_OK,false,800)==NAV_CONFIRM_EXHAUST);
    UiNavigation_Key(&n,KEY_DOWN,false,800);
    assert(UiNavigation_Key(&n,KEY_OK,false,800)==NAV_RESET);
    UiNavigation_Key(&n,KEY_DOWN,false,800);
    assert(UiNavigation_Key(&n,KEY_OK,false,800)==NAV_PAGE && n.page==UI_PAGE_HOME);
    n.page=UI_PAGE_SERVICE;n.focus=0;
    UiNavigation_Key(&n,KEY_UP,false,800);assert(n.focus==6);
    assert(UiNavigation_Key(&n,KEY_CANCEL,false,800)==NAV_PAGE && n.page==UI_PAGE_HOME);
}
static void Logs(void)
{
    DiagLog_Init(); DiagEntry e;
    assert(!DiagLog_Read(0,&e));
    for (unsigned i=0;i<200;i++) DiagLog_Write(i,(DiagLevel)(i%4U),"TC","sample %u",i);
    assert(DiagLog_Count()==128 && DiagLog_Overwritten()==72 && DiagLog_Sequence()==200);
    assert(DiagLog_Read(0,&e) && e.ms==72 && e.sequence==73);
    assert(DiagLog_Read(127,&e) && e.ms==199);
    assert(!DiagLog_Read(128,&e));
    char long_text[200]; memset(long_text,'a',sizeof(long_text)); long_text[199]=0;
    DiagLog_Write(250,DIAG_ERROR,"LONGMODULE","%s",long_text);
    assert(DiagLog_Read(127,&e) && strlen(e.text)==DIAG_LOG_MESSAGE-1U && strlen(e.tag)==7U);
    DiagLog_Write(300,DIAG_INFO,"SYS","x\ny\tz");
    assert(DiagLog_Read(127,&e) && !strcmp(e.text,"x y z"));
}
static void HomeCommands(void)
{
    /* Connect actual navigation actions to actual control engine; no HAL/GPIO. */
    BurnerConfig c={.commissioned=true,.purge_ms=30000,.required_purge_ms=30000,
      .interpurge_ms=30000,.postpurge_ms=60000,.ignition_ms=5000,.flame_stable_ms=500,
      .airflow_timeout_ms=5000,.input_max_age_ms=500,.co_min_wait_ms=1000,
      .co_timeout_ms=60000,.co_limit_ppm=10,.co2_hysteresis_ppm=100,.max_attempts=2,
      .cooling_mode=COOL_BY_TIME};
    BurnerInputs in={.captured_ms=0};BurnerControl b;Burner_Init(&b,&c,0);
    UiNavigation n;UiNavigation_Init(&n);
    assert(UiNavigation_Key(&n,KEY_UP,false,800)==NAV_NONE);
    assert(b.state==BURNER_IDLE&&!b.out.gas);
    assert(UiNavigation_Key(&n,KEY_OK,false,800)==NAV_START);
    assert(Burner_RequestStart(&b,BURNER_HMI,&in,0)==BURNER_COMMAND_ACCEPTED);
    assert(b.state==BURNER_PRECHECK&&!b.out.gas);
    assert(UiNavigation_Key(&n,KEY_OK,false,800)==NAV_START);
    assert(Burner_RequestStart(&b,BURNER_HMI,&in,0)==BURNER_COMMAND_BUSY);
    assert(UiNavigation_Key(&n,KEY_DOWN,false,800)==NAV_NONE&&b.state==BURNER_PRECHECK);
    assert(UiNavigation_Key(&n,KEY_OK,false,800)==NAV_STOP);
    Burner_RequestStop(&b,BURNER_HMI,0);assert(b.state==BURNER_SHUTDOWN&&!b.out.gas&&!b.out.ignition);
    c.commissioned=false;Burner_Init(&b,&c,0);
    UiNavigation_Key(&n,KEY_UP,false,800);
    assert(UiNavigation_Key(&n,KEY_OK,false,800)==NAV_START);
    assert(Burner_RequestStart(&b,BURNER_HMI,&in,0)==BURNER_COMMAND_CONFIG_REQUIRED&&b.state==BURNER_IDLE);
    KeyInput k;Armed(&k,0);Advance(&k,KEY_MASK(KEY_OK),5100);
    KeyCode e;assert(KeyInput_Read(&k,&e)&&e==KEY_DEBUG);
    assert(UiNavigation_Key(&n,e,false,800)==NAV_PAGE&&n.page==UI_PAGE_DEBUG);
    Advance(&k,0,40);Empty(&k); /* DIAG hold/release never emits START */
    puts("PASS Home commands: UP RUN / DOWN STOP, OK-only dispatch, long OK DIAG, busy/config rejection and immediate logical STOP");
}
int main(void)
{
    Keys(0); Keys(UINT32_MAX-2000U); Navigation(); HomeCommands();Logs();
    puts("PASS: key bounce/repeat/5s hold/boot guard/gap/wrap; Home RUN/STOP; transactional Settings; bounded logs");
    return 0;
}
