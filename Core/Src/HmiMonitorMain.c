#if defined(FW_CUSTOM_HMI)
#include "main.h"
#include "gpio.h"
#include "dma.h"
#include "spi.h"
#include "tim.h"
#include "adc.h"
#include "usart.h"
#include "App.h"
#include "InternalAssets.h"
#include "NRF24L01.h"
#include "co2_link.h"
#include "Board595Map.h"
#include "HmiDiag.h"
#include "HmiSt7796.h"
#include "HmiDisplayConfig.h"
#include "HmiMonitor.h"
#include "HmiMonitorIo.h"
#include "HmiMonitorFault.h"
#include "HmiDiagData.h"
#include "stm32f4xx_ll_adc.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#if defined(FW_FINAL_APPLICATION)
#include "BoardRuntime.h"
#include "BoardActuation.h"
#include "UiNavigation.h"
#include "HmiPages.h"
#include "HmiHome.h"
#include "HmiAssetDma.h"
#include "BoardBus.h"
static UiNavigation navigation;
static HmiPageView page_view;
static HmiPagesDma page_dma;
static HmiAssetDma asset_dma;
static bool asset_dma_fallback;
static HmiDiagView menu_view;
static const HmiPage *active_page;
static int16_t route_focus=-1;
static char menu_message[64];
static char home_command_message[48];
static uint32_t home_command_ms;
static bool home_command_visible;
static uint8_t painted_kind=255;
#endif

extern const HmiFont monitor_font,monitor_fallback_font;
static const HmiFont *font;
static HmiSt7796 lcd;
static HmiStream stream;
static uint16_t first[HMI_DISPLAY_BUFFER_PIXELS],second[HMI_DISPLAY_BUFFER_PIXELS];
static KeyInput keys;
static volatile bool keys_ready;
static HmiDiag diag;
static HmiDiagView view,previous;
static NRF24_Radio radio;
static CO2_Snapshot co2_cache;
static MAX6675_Snapshot_t temperatures;
static bool assets_ok,radio_ok,painted,page_changed=true,render_diag;
static uint32_t last_capture,health_ms,last_bus_errors;
static uint32_t scan_ms,render_revision;
static uint8_t prior_pressed,tone;
static uint8_t last_nrf[32];
static uint32_t last_nrf_ms,nrf_frames,loop_cycles,render_cycles,capture_cycles;
static uint32_t last_loop_cycles;
static uint32_t display_job_start,last_display_job_ms,max_display_job_ms,last_full_page_ms;
static bool display_job_full;
static uint32_t tone_ms;
static char home_lines[9][HMI_DIAG_COLUMNS];
static char previous_home[9][HMI_DIAG_COLUMNS];
#if defined(FW_FINAL_APPLICATION)
static void PageChanged(void)
{
    page_changed=true;HmiStream_Cancel(&stream);
    uint32_t mask=__get_PRIMASK();__disable_irq();KeyInput_RequireRelease(&keys);__set_PRIMASK(mask);
}
static void StopManualMotor(void)
{
    BoardActuation_StopFanTuning();
    BoardActuation_StopRelayTest();
    BurnerAppSnapshot b;BurnerApp_GetSnapshot(&b);
    if(b.state==BURNER_IDLE&&DamperStepper_IsRunning())BoardActuation_Abort();
}
static void ApplicationKey(KeyCode key)
{
    BurnerAppSnapshot b;BurnerApp_GetSnapshot(&b);
    if(navigation.page==UI_PAGE_HOME&&assets_ok&&active_page&&strcmp(active_page->id,"home")) {
        if(key==KEY_UP||key==KEY_DOWN) {
            unsigned n=active_page->count;
            for(unsigned i=0;i<n;++i) {
                int next=route_focus+(key==KEY_UP?-1:1);
                if(next<0)next=(int)n-1;
                if(next>=(int)n)next=0;
                route_focus=(int16_t)next;
                if(active_page->widgets[next].target_page) {PageChanged();return;}
            }
            route_focus=-1;
        }
        if(key==KEY_OK&&route_focus>=0) {
            const char *target=active_page->widgets[route_focus].target_page;
            if(target) {active_page=HmiPages_Find(hmi_pages,hmi_page_count,target);route_focus=-1;PageChanged();return;}
        }
        if(key==KEY_CANCEL) {active_page=HmiPages_Find(hmi_pages,hmi_page_count,"home");route_focus=-1;navigation.focus=1;PageChanged();return;}
    }
    UiPage before=navigation.page;
    UiAction action=UiNavigation_Key(&navigation,key,b.mode==BURNER_AUTO_MODE,b.co2_target_ppm);
    if(before==UI_PAGE_SERVICE&&navigation.page!=UI_PAGE_SERVICE)StopManualMotor();
    if(before==UI_PAGE_FAN&&navigation.page!=UI_PAGE_FAN)BoardActuation_StopFanTuning();
    switch(action) {
    case NAV_FAN_APPLY: {
        bool ok=BoardActuation_TuneFan(navigation.fan_ccr,b.state==BURNER_IDLE,HAL_GetTick());
        snprintf(menu_message,sizeof(menu_message),"%s",ok?"CCR applied; measure RPM":"Refused: IDLE, ZC, relays OFF, motor stopped");
        DiagLog_Write(HAL_GetTick(),ok?DIAG_INFO:DIAG_WARN,"FANTUNE","CCR=%u accepted=%u",navigation.fan_ccr,ok);
        break;
    }
    case NAV_FAN_LIMITS:
        BoardActuation_StopFanTuning();
        snprintf(menu_message,sizeof(menu_message),"Limits saved in RAM; fan OFF; apply CCR again");break;
    case NAV_FAN_OFF:
        BoardActuation_StopFanTuning();
        snprintf(menu_message,sizeof(menu_message),"Fan OFF");break;
    case NAV_SAVE_MODE:(void)BurnerApp_SetMode(navigation.draft_auto?BURNER_AUTO_MODE:BURNER_MANUAL_MODE);break;
    case NAV_SAVE_CO2:(void)BurnerApp_SetCO2Target(navigation.draft_co2);break;
    case NAV_START: {
        BoardActuation_StopRelayTest();
        BurnerCommandResult result=BurnerApp_Start(BURNER_HMI);
        BurnerApp_GetSnapshot(&b);
        snprintf(menu_message,sizeof(menu_message),"START: %s",result==BURNER_COMMAND_ACCEPTED?"ACCEPTED":result==BURNER_COMMAND_BUSY?"BUSY / LOCKOUT":Burner_StartBlockText(b.start_blockers));
        snprintf(home_command_message,sizeof(home_command_message),"%.47s",menu_message);
        home_command_ms=HAL_GetTick();home_command_visible=true;break;
    }
    case NAV_STOP:
        BoardActuation_StopRelayTest();
        BurnerApp_Stop(BURNER_HMI);
        snprintf(menu_message,sizeof(menu_message),"STOP: %s",b.state==BURNER_IDLE?"ALREADY IDLE":b.state==BURNER_LOCKOUT?"FAULT REMAINS":"SHUTDOWN REQUESTED");
        snprintf(home_command_message,sizeof(home_command_message),"%.47s",menu_message);
        home_command_ms=HAL_GetTick();home_command_visible=true;break;
    case NAV_STEP_PLUS:case NAV_STEP_MINUS: {
        int64_t endpoint=(int64_t)DamperStepper_GetPosition()+(action==NAV_STEP_PLUS?160:-160);
        HAL_StatusTypeDef result=HAL_BUSY;
        if(b.state==BURNER_IDLE&&!b.requested.gas&&!(HC595_GetShadow()&0x82U)&&!DamperStepper_IsRunning()&&endpoint>=INT32_MIN&&endpoint<=INT32_MAX) {
            result=BoardActuation_ManualStep(action==NAV_STEP_PLUS?160:-160);
        }
        snprintf(menu_message,sizeof(menu_message),"Motor: %s",result==HAL_OK?"160-pulse move; NOT HOMED":result==HAL_ERROR?"driver init / speed / endpoint error":"refused busy / unsafe state");
        DiagLog_Write(HAL_GetTick(),result==HAL_OK?DIAG_INFO:DIAG_WARN,"SERVICE","Motor target=%ld result=%u",(long)endpoint,(unsigned)result);break;
    }
    case NAV_MOTOR_STOP:
        BoardActuation_StopRelayTest();
        if(b.state==BURNER_IDLE) {BoardActuation_Abort();snprintf(menu_message,sizeof(menu_message),"Motor stopped; ref UNKNOWN");}
        else snprintf(menu_message,sizeof(menu_message),"Use device STOP while process owns motor");
        break;
    case NAV_CONFIRM_EXHAUST: {
        bool ok=b.state==BURNER_IDLE&&!b.requested.gas&&!(HC595_GetShadow()&0x82U)&&BoardActuation_ConfirmExhaust();
        snprintf(menu_message,sizeof(menu_message),"EXHAUST reference: %s",ok?"confirmed by operator":"refused: IDLE/motor/gas/ign check");
        DiagLog_Write(HAL_GetTick(),ok?DIAG_WARN:DIAG_ERROR,"DAMPER","Operator EXHAUST reference accepted=%u",ok);break;
    }
    case NAV_RELAY_TEST:
        if(BoardActuation_RelayTestActive()) {BoardActuation_StopRelayTest();snprintf(menu_message,sizeof(menu_message),"Relay test stopped; all OFF");}
        else snprintf(menu_message,sizeof(menu_message),"%s",BoardActuation_StartRelayTest(b.state==BURNER_IDLE&&!b.requested.gas,HAL_GetTick())?"Q1..Q7: add every 3s; all OFF at 21s":"Relay test refused: IDLE + motor stopped required");
        break;
    case NAV_RESET:
        snprintf(menu_message,sizeof(menu_message),"%s",BurnerApp_AcknowledgeFault()?"Fault acknowledged; START required":"Reset refused: conditions not satisfied");break;
    default:break;
    }
    if(action==NAV_PAGE) {menu_message[0]='\0';PageChanged();}
    else if(before==UI_PAGE_HOME&&navigation.page==UI_PAGE_HOME) {
        last_capture=HAL_GetTick()-100U; /* old/new focus rectangles only; don't cancel DMA */
    } else {page_changed=true;HmiStream_Cancel(&stream);}
}
#endif

void HmiMonitor_KeyTick(void)
{
    if(!keys_ready)return;
    uint32_t now=HAL_GetTick();if(now-scan_ms<5U)return;scan_ms=now;
    uint8_t raw=0;
    if(HAL_GPIO_ReadPin(OK_GPIO_Port,OK_Pin)==GPIO_PIN_RESET)raw|=KEY_MASK(KEY_OK);
    if(HAL_GPIO_ReadPin(BACK_GPIO_Port,BACK_Pin)==GPIO_PIN_RESET)raw|=KEY_MASK(KEY_CANCEL);
    if(HAL_GPIO_ReadPin(UP_GPIO_Port,UP_Pin)==GPIO_PIN_RESET)raw|=KEY_MASK(KEY_UP);
    if(HAL_GPIO_ReadPin(DOWN_GPIO_Port,DOWN_Pin)==GPIO_PIN_RESET)raw|=KEY_MASK(KEY_DOWN);
    KeyInput_Update(&keys,raw,now);
}
static void Keys(void)
{
    uint32_t now=HAL_GetTick();
    if(tone && now-tone_ms>=100U) {
        tone_ms=now;
        if(tone==1U) {(void)Buzzer_Play(2600,70);tone=2;}
        else if(tone==2U) {(void)Buzzer_Play(3400,100);tone=3;}
        else tone=0;
    }
    uint8_t pressed=keys.stable,down=pressed&~prior_pressed;prior_pressed=pressed;
    if(keys.armed && down && !tone)(void)Buzzer_Play(2100,40);
    for(unsigned budget=0;budget<16;++budget) {
        KeyCode key;uint32_t mask=__get_PRIMASK();__disable_irq();
        bool available=KeyInput_Read(&keys,&key);__set_PRIMASK(mask);
        if(!available)break;
        if(HmiDiag_Key(&diag,key)) {
#if defined(FW_FINAL_APPLICATION)
            navigation.output_confirm=false;
            if(navigation.page==UI_PAGE_SERVICE||navigation.page==UI_PAGE_FAN)StopManualMotor();
            if(!diag.open) {navigation.page=UI_PAGE_HOME;navigation.focus=1;navigation.editing=false;route_focus=-1;if(assets_ok)active_page=HmiPages_Find(hmi_pages,hmi_page_count,"home");}
#endif
            mask=__get_PRIMASK();__disable_irq();KeyInput_RequireRelease(&keys);__set_PRIMASK(mask);
            page_changed=true;
            HmiStream_Cancel(&stream);
            DiagLog_Write(now,DIAG_INFO,"UI",diag.open?"DIAG entered: OK held 5s":"HOME: Cancel");
            if(diag.open) {(void)Buzzer_Play(1800,70);tone=1;tone_ms=now;}
        }
#if defined(FW_FINAL_APPLICATION)
        else if(!diag.open&&key!=KEY_DEBUG)ApplicationKey(key);
#endif
    }
}
static void Temperature(char *text,size_t size,const MAX6675_Data_t *d)
{
    if(d->valid) {
        char number[20];(void)HmiText_Q4(number,sizeof(number),d->temperature_q4);
        snprintf(text,size,"%s C",number);
    } else snprintf(text,size,"%s",d->last_status==MAX6675_OPEN_THERMOCOUPLE?"OPEN":
        d->last_status==MAX6675_SPI_ERROR?"SPI ERROR":d->has_valid_sample&&d->stale?"STALE":"NO SAMPLE");
}
static void CaptureHome(const CO2_Snapshot *co2)
{
    char t1[24],t2[24];Temperature(t1,sizeof(t1),&temperatures.sensor[0]);Temperature(t2,sizeof(t2),&temperatures.sensor[1]);
    snprintf(home_lines[0],sizeof(home_lines[0]),"TC1 Flame: %s",t1);
    snprintf(home_lines[1],sizeof(home_lines[1]),"TC2 Sample cooler: %s",t2);
    if(co2->valid)snprintf(home_lines[2],sizeof(home_lines[2]),"CO2: %u ppm",co2->ppm);
    else snprintf(home_lines[2],sizeof(home_lines[2]),"CO2: %s",!radio_ok||!radio.ready?"NRF OFF":co2->link_stale?"NO LINK":"INVALID / WARMUP");
    snprintf(home_lines[3],sizeof(home_lines[3]),"CO: UNCALIBRATED | burner INHIBITED");
    snprintf(home_lines[4],sizeof(home_lines[4]),"1 shared stepper / 2 opposed dampers");
    snprintf(home_lines[5],sizeof(home_lines[5]),"Q5 distribution fan | PA8 combustion fan");
    snprintf(home_lines[6],sizeof(home_lines[6]),"Fuel / ignition / relay commands OFF");
    snprintf(home_lines[7],sizeof(home_lines[7]),"QSPI assets: %s",assets_ok?"CRC OK":"ERROR - INTERNAL FALLBACK");
    snprintf(home_lines[8],sizeof(home_lines[8]),"NEW HMI MONITOR / not commissioned");
#if defined(FW_FINAL_APPLICATION)
    BurnerAppSnapshot b;BurnerApp_GetSnapshot(&b);
    snprintf(home_lines[3],sizeof(home_lines[3]),"%s %s / %s",b.output_test?"OUTPUT TEST":b.simulated?"DRY RUN":"Burner",b.configured?Burner_StateText(b.state):"CONFIG REQUIRED",b.mode==BURNER_AUTO_MODE?"AUTO":"MANUAL");
    snprintf(home_lines[5],sizeof(home_lines[5]),"%s RUN    %s STOP",navigation.focus==0?">":" ",navigation.focus==1?">":" ");
    snprintf(home_lines[6],sizeof(home_lines[6]),"Request GAS=%u IGN=%u FAN=%u",b.requested.gas,b.requested.ignition,b.requested.combustion_fan);
    if(home_command_visible&&HAL_GetTick()-home_command_ms<4000U)
        snprintf(home_lines[6],sizeof(home_lines[6]),"%s",home_command_message);
    snprintf(home_lines[8],sizeof(home_lines[8]),"%s / %s",b.output_test?"OUTPUT TEST - NO FUEL":b.simulated?"DRY RUN - physical outputs OFF":"FINAL V1",Burner_FaultText(b.fault));
#endif
}
static HmiResult Home(void *unused,HmiRect tile,HmiSurface *surface)
{
    (void)unused;
    HmiResult r=HmiSurface_Fill(surface,(HmiRect){0,0,tile.w,tile.h},0x0841);
    if(r!=HMI_OK)return r;
    HmiTextStyle style={0xffff,0,HMI_TEXT_LEFT,false};
    r=HmiText_Draw(surface,(HmiRect){8-tile.x,8-tile.y,464,font->height},font,
                   "SYSTEM MONITOR",sizeof("SYSTEM MONITOR"),style);
    if(r!=HMI_OK)return r;
    for(unsigned i=0;i<9;++i) {
        style.foreground=(i==3U||(i==7U&&!assets_ok))?0xfe88:0x8df7;
        r=HmiText_Draw(surface,(HmiRect){8-tile.x,42+(int32_t)i*24-tile.y,464,font->height},font,
                       home_lines[i],sizeof(home_lines[i]),style);
        if(r!=HMI_OK)return r;
    }
    style.foreground=0xffff;
    return HMI_OK;
}
static void Health(uint32_t now)
{
    if(now-health_ms<1000U)return;
    health_ms=now;
    HC595_Diagnostics bus;HC595_GetDiagnostics(&bus);
    if(bus.errors!=last_bus_errors) {
        last_bus_errors=bus.errors;
        DiagLog_Write(now,DIAG_ERROR,"SPI2","errors=%lu code=%08lX shadow=%02X latched=%02X",
            (unsigned long)bus.errors,(unsigned long)bus.last_error,bus.shadow,bus.latched);
    }
    static bool seen[2];
    static uint32_t previous_health[2];
    for(unsigned i=0;i<2;++i) {
        const MAX6675_Data_t *d=&temperatures.sensor[i];
        uint32_t health=(uint32_t)d->last_status|((uint32_t)d->valid<<8)|
            ((uint32_t)d->stale<<9)|((uint32_t)d->has_valid_sample<<10);
        if(!seen[i]||health!=previous_health[i]) {
            seen[i]=true;previous_health[i]=health;
            DiagLog_Write(now,d->valid?DIAG_INFO:DIAG_WARN,i?"TC2":"TC1",
                "status=%u valid=%u stale=%u has=%u",
                (unsigned)d->last_status,d->valid,d->stale,d->has_valid_sample);
        }
    }
}
static bool ChangedRect(HmiRect *r)
{
    unsigned first_row=HMI_DIAG_LINES,last_row=0;
    bool header=strcmp(view.header,previous.header)||strcmp(view.status,previous.status);
    bool footer=strcmp(view.footer,previous.footer);
    for(unsigned i=0;i<view.line_count;++i)
        if(strcmp(view.lines[i],previous.lines[i])||view.levels[i]!=previous.levels[i]) {
            if(first_row==HMI_DIAG_LINES)first_row=i;
            last_row=i;
        }
    if(!header && !footer && first_row==HMI_DIAG_LINES)return false;
    int top=header?0:8+(int)(first_row+2U)*font->line_height;
    int bottom=footer?320:8+(int)(last_row+3U)*font->line_height;
    if(first_row==HMI_DIAG_LINES && !footer)bottom=8+2*font->line_height;
    if(!header && first_row==HMI_DIAG_LINES)top=320-8-font->height;
    if(bottom>320)bottom=320;
    *r=(HmiRect){0,top,480,(uint16_t)(bottom-top)};return true;
}
static unsigned live_index;
static void LiveLine(DiagLevel level,const char *format,...) __attribute__((format(printf,2,3)));
static void LiveLine(DiagLevel level,const char *format,...)
{
    char text[128];
    va_list args;va_start(args,format);
    vsnprintf(text,sizeof(text),format,args);va_end(args);
    size_t length=strlen(text),columns=view.columns;
    if(!length)length=1;
    for(size_t offset=0;offset<length;offset+=columns) {
        unsigned row=live_index++;
        if(row<diag.scroll || row-diag.scroll>=view.line_count)continue;
        row-=diag.scroll;
        size_t n=length-offset;if(n>columns)n=columns;
        memcpy(view.lines[row],text+offset,n);view.lines[row][n]='\0';view.levels[row]=level;
    }
}
static void HexRows(const char *prefix,const uint8_t *data,unsigned length)
{
    if(!length) {LiveLine(DIAG_WARN,"%s: NO DATA",prefix);return;}
    for(unsigned offset=0;offset<length;offset+=8) {
        char text[48];int n=snprintf(text,sizeof(text),"%s %02u:",prefix,offset);
        for(unsigned i=offset;i<length && i<offset+8 && n>0 && (unsigned)n<sizeof(text);++i)
            n+=snprintf(text+n,sizeof(text)-(unsigned)n," %02X",data[i]);
        LiveLine(DIAG_INFO,"%s",text);
    }
}
#if defined(FW_FINAL_APPLICATION)
static const char *DiagSource(BurnerSource source)
{return source==BURNER_PLC?"PLC":source==BURNER_AUTO?"AUTO":"HMI";}
static const char *FaultAction(BurnerFault fault)
{
    switch(fault) {
    case BURNER_NO_FAULT:return "No latched fault";
    case BURNER_FALSE_FLAME:return "Check PA1: flame before ignition";
    case BURNER_FLAME_LOST:return "Check flame detector and fuel supply";
    case BURNER_IGNITION_FAILED:return "Check ignition, fuel and flame signal";
    case BURNER_INPUT_STALE:return "Check TC1 validity and input ages";
    case BURNER_ACTUATION:return "Check output inhibits and damper";
    case BURNER_AIRFLOW:return "Check fan sync and airflow input";
    case BURNER_CO_HIGH:return "Check combustion and CO measurement";
    case BURNER_CO_TIMEOUT:return "Check CO calibration and sample path";
    case BURNER_COOLING_TIMEOUT:return "Check TC2 and sample cooler fan";
    case BURNER_TEMPERATURE:return "Check TC1 and combustion temperature";
    case BURNER_ADJUST_LIMIT:return "Air adjustment limit; inspect burner";
    case BURNER_CONFIGURATION:return "Check effective process settings";
    default:return "Check interlocks and damper feedback";
    }
}
static const char *StageWait(const BurnerAppSnapshot *b)
{
    if(b->state==BURNER_IDLE)return "Waiting START command";
    if(b->state==BURNER_LOCKOUT)return "Resolve cause; acknowledge fault";
    if(b->state==BURNER_PRECHECK)return "Checking no-flame / outputs OFF";
    if(b->state==BURNER_PURGE||b->state==BURNER_INTERPURGE)
        return b->timing.purge_counting?"Logical purge timer running":"Waiting fan request + sync";
    if(b->state==BURNER_IGNITION_1||b->state==BURNER_IGNITION_2)return "Waiting flame stable for 500ms";
    if(b->state==BURNER_SHUTDOWN||b->state==BURNER_POSTPURGE)return "Postpurge then outputs OFF";
    switch(b->sample_state) {
    case SAMPLE_WAIT_BYPASS:return "Waiting EXHAUST position";
    case SAMPLE_WAIT_TEMPERATURE:return temperatures.sensor[0].valid?"Waiting initial sample delay":"Waiting valid TC1 temperature";
    case SAMPLE_VALVE_1:return "Filling sample; valve 1 open";
    case SAMPLE_COOLING:return "Waiting valid TC2 below 40 C";
    case SAMPLE_VALVE_2:return "Transferring sample; valve 2 open";
    case SAMPLE_WAIT_CO:return "Waiting fresh calibrated CO";
    case SAMPLE_AIR_SETTLE:return "Waiting air adjustment to settle";
    case SAMPLE_ACCEPTED:return b->process_proven?"Distribution; periodic CO recheck":"Waiting GREENHOUSE position";
    default:return "Starting sample cycle";
    }
}
#endif
static void CaptureLive(uint32_t now,const CO2_Snapshot *co2)
{
    uint32_t cycle_start=DWT->CYCCNT;
    static const char *names[]={"SUMMARY","EVENTS","OUTPUTS","SENSORS","COMMS","ADV GPIO","ADV ADC","ADV SYSTEM"};
    memset(view.lines,0,sizeof(view.lines));memset(view.levels,0,sizeof(view.levels));live_index=0;
    snprintf(view.header,sizeof(view.header),"DIAG %u/%u | %s | LIVE",(unsigned)diag.section+1U,HMI_DIAG_SECTIONS,names[diag.section]);
    snprintf(view.footer,sizeof(view.footer),"OK next | UP/DN scroll | Cancel HOME");
    HmiMonitorIoSnapshot io;HmiMonitorIo_Get(&io);
#if defined(FW_FINAL_APPLICATION)
    BoardBusSnapshot serial_bus;BoardBus_Get(&serial_bus);
    BoardAnalogValues analog_values;uint32_t analog_time;BoardRuntime_GetAnalog(&analog_values,&analog_time);
#endif
    HC595_Diagnostics bus;HC595_GetDiagnostics(&bus);
    if(diag.section==HMI_DIAG_INPUTS) {
        LiveLine(DIAG_INFO,"GPIO electrical level; not safety proof");
        LiveLine(DIAG_INFO,"OK PC14=%lu CANCEL PC13=%lu [LOW=pressed]",(GPIOC->IDR>>14)&1UL,(GPIOC->IDR>>13)&1UL);
        LiveLine(DIAG_INFO,"UP PB8=%lu DOWN PB9=%lu [LOW=pressed]",(GPIOB->IDR>>8)&1UL,(GPIOB->IDR>>9)&1UL);
        LiveLine(DIAG_INFO,"FLAME PA1=%lu ZC PA9=%lu",(GPIOA->IDR>>1)&1UL,(GPIOA->IDR>>9)&1UL);
        LiveLine(DIAG_INFO,"TIM1 CNT=%lu CCR1=%lu ARR=%lu PSC=%lu",
            (unsigned long)TIM1->CNT,(unsigned long)TIM1->CCR1,(unsigned long)TIM1->ARR,(unsigned long)TIM1->PSC);
        LiveLine(DIAG_INFO,"PA9 rising -> TIM1 reset; PA8 PWM2; 30us/tick");
        LiveLine(DIAG_INFO,"HALL PB12=%lu; active LOW",(GPIOB->IDR>>12)&1UL);
        LiveLine(DIAG_INFO,"PC0=%lu PC1=%lu PB2=%lu [legacy inputs]",GPIOC->IDR&1UL,(GPIOC->IDR>>1)&1UL,(GPIOB->IDR>>2)&1UL);
        LiveLine(DIAG_INFO,"FLT PB14=%lu PB15=%lu [raw]",(GPIOB->IDR>>14)&1UL,(GPIOB->IDR>>15)&1UL);
        LiveLine(DIAG_INFO,"GPIOA IDR=%04lX GPIOB IDR=%04lX",GPIOA->IDR&0xffffUL,GPIOB->IDR&0xffffUL);
        LiveLine(DIAG_INFO,"GPIOC IDR=%04lX GPIOD IDR=%04lX",GPIOC->IDR&0xffffUL,GPIOD->IDR&0xffffUL);
        LiveLine(DIAG_INFO,"Debounced keys=%X armed=%u",keys.stable,keys.armed);
        LiveLine(DIAG_WARN,"No airflow / gas-closed proof configured");
        LiveLine(DIAG_INFO,"GPIOA ODR=%04lX GPIOB ODR=%04lX",GPIOA->ODR&0xffffUL,GPIOB->ODR&0xffffUL);
        LiveLine(DIAG_INFO,"GPIOC ODR=%04lX GPIOD ODR=%04lX",GPIOC->ODR&0xffffUL,GPIOD->ODR&0xffffUL);
        LiveLine(DIAG_INFO,"STEP PC8=%u DIR PC9=%u",(unsigned)((GPIOC->IDR>>8)&1U),(unsigned)((GPIOC->ODR>>9)&1U));
        LiveLine(DIAG_INFO,"EN PC6=%u PC7=%u [LOW=enable]",(unsigned)((GPIOC->ODR>>6)&1U),(unsigned)((GPIOC->ODR>>7)&1U));
        LiveLine(DIAG_INFO,"LCD CS PD2=%u DC PC12=%u RST PC10=%u",(unsigned)((GPIOD->ODR>>2)&1U),(unsigned)((GPIOC->ODR>>12)&1U),(unsigned)((GPIOC->ODR>>10)&1U));
        LiveLine(DIAG_INFO,"NRF CE PA11=%u CSN PA15=%u",(unsigned)((GPIOA->ODR>>11)&1U),(unsigned)((GPIOA->ODR>>15)&1U));
        LiveLine(DIAG_INFO,"595 LATCH PA0=%u Q0 SELECT=%u",(unsigned)(GPIOA->ODR&1U),bus.latched&1U);
    } else if(diag.section==HMI_DIAG_OUTPUTS) {
#if defined(FW_FINAL_APPLICATION)
        BurnerAppSnapshot b;BurnerApp_GetSnapshot(&b);
        BoardActuationSnapshot act;BoardActuation_Get(&act);
        LiveLine(act.inhibited?DIAG_WARN:DIAG_INFO,"Outputs: %s",act.inhibited?"INHIBITED":"PERMITTED");
        if(act.inhibited)LiveLine(DIAG_WARN,"Reason: %s",BoardActuation_InhibitText(act.inhibit_flags));
        LiveLine(DIAG_INFO,"Fan req=%u CCR=%lu gate=%u",b.requested.combustion_fan,
            (unsigned long)TIM1->CCR1,(unsigned)((TIM1->BDTR&TIM_BDTR_MOE)!=0));
        LiveLine(DIAG_INFO,"REF=%u ZC sync=%u table=%u fault=%u",act.referenced,act.fan_sync,act.fan_calibrated,act.fault);
        const unsigned order[]={7,1,2,5,6,3,4};
        const char *labels[]={"Gas Q7","Ignition Q1","Exchanger Q2","Distribution Q5","Sample cooler Q6","Sample valve 1 Q3","Sample valve 2 Q4"};
        const bool req[]={b.requested.gas,b.requested.ignition,b.requested.exchanger_fan,b.requested.distribution_fan,
            b.requested.cooler_fan,b.requested.sample_valve_1,b.requested.sample_valve_2};
        for(unsigned i=0;i<7;i++)LiveLine(DIAG_INFO,"%s req=%u queued=%u sent=%u",labels[i],req[i],
            (bus.shadow>>order[i])&1U,(bus.latched>>order[i])&1U);
        LiveLine(DIAG_WARN,"Sent image is NOT physical relay feedback");
        LiveLine(DIAG_INFO,"Damper REF=%u EXH=%u GREEN=%u",act.referenced,act.bypass_proven,act.process_proven);
        LiveLine(DIAG_INFO,"Motor running=%u position=%ld",DamperStepper_IsRunning(),(long)DamperStepper_GetPosition());
        for(unsigned bit=0;bit<6;bit++)if(act.inhibit_flags&(1U<<bit))
            LiveLine(DIAG_WARN,"Inhibit: %s",BoardActuation_InhibitText(1U<<bit));
#else
        LiveLine(DIAG_INFO,"Relay image queued=%02X sent=%02X",bus.shadow,bus.latched);
        LiveLine(DIAG_WARN,"Physical relay feedback unavailable");
#endif
    } else if(diag.section==HMI_DIAG_ADC) {
        bool fresh=io.adc_sequence && now-io.adc_ms<500U;
        uint16_t cal=*VREFINT_CAL_ADDR;
        HmiDiagAnalog analog;HmiDiag_Analog(io.raw,fresh,cal,*TEMPSENSOR_CAL1_ADDR,*TEMPSENSOR_CAL2_ADDR,&analog);
        uint32_t vdda=analog.vdda_mv;bool volts=analog.voltage_valid;
#if defined(FW_FINAL_APPLICATION)
        if(analog_values.co_valid)LiveLine(DIAG_INFO,"CO=%u ppm age=%lu ms",analog_values.co_ppm,(unsigned long)(now-analog_time));
        else LiveLine(DIAG_WARN,"CO ppm=UNCAL / INVALID");
        if(analog_values.me2_temperature_valid)LiveLine(DIAG_INFO,"ME2 NTC=%ld x0.25 C",(long)analog_values.me2_temperature_q4);
        else LiveLine(DIAG_WARN,"ME2 NTC temperature=UNCAL / INVALID");
        if(analog_values.mixer_temperature_valid)LiveLine(DIAG_INFO,"Mixer NTC=%ld x0.25 C",(long)analog_values.mixer_temperature_q4);
        else LiveLine(DIAG_WARN,"Mixer NTC temperature=UNCAL / INVALID");
#else
        LiveLine(DIAG_WARN,"CO ppm=UNCAL; zero/span/Tcorr pending");
        LiveLine(DIAG_WARN,"NTC degrees=UNCAL; curve/Beta pending");
#endif
        LiveLine(fresh?DIAG_INFO:DIAG_ERROR,"ADC seq=%lu age=%lu ms %s",(unsigned long)io.adc_sequence,
            (unsigned long)(io.adc_sequence?now-io.adc_ms:0U),fresh?"VALID":"NO DATA / STALE");
        LiveLine(DIAG_INFO,"TIM5_CH1 internal PWM 20Hz -> ADC1 DMA S0");
        const char *labels[]={"PA2 ME2sig","PA3 ME2diff","PA4 ME2ntc","PA5 mixer NTC","VREFINT","CPU TEMP"};
        for(unsigned i=0;i<6;++i) {
            if(volts)LiveLine(DIAG_INFO,"%s raw=%u mV=%lu",labels[i],io.raw[i],(unsigned long)((uint32_t)io.raw[i]*vdda/4095U));
            else LiveLine(DIAG_WARN,"%s raw=%u mV=INVALID",labels[i],io.raw[i]);
        }
        LiveLine(volts?DIAG_INFO:DIAG_WARN,"VDDA=%lu mV VREFcal=%u",(unsigned long)vdda,cal);
        LiveLine(DIAG_INFO,"ME2 differential raw = %ld",(long)((int32_t)io.raw[0]-io.raw[1]));
        if(volts)LiveLine(DIAG_INFO,"ME2 dV=%ld mV",(long)(((int32_t)io.raw[0]-io.raw[1])*(int32_t)vdda/4095));
        LiveLine(DIAG_INFO,"ADC errors=%lu started=%u",(unsigned long)io.adc_errors,io.adc_started);
        if(analog.cpu_temperature_valid) {
            LiveLine(DIAG_INFO,"CPU temp = %ld x 0.1 C",(long)analog.cpu_temperature_tenths);
        }
    } else if(diag.section==HMI_DIAG_SENSORS) {
        LiveLine(DIAG_INFO,"Flame PA1: %s",(GPIOA->IDR&2U)?"DETECTED":"NOT DETECTED");
#if defined(FW_FINAL_APPLICATION)
        if(analog_values.co_valid)LiveLine(DIAG_INFO,"CO=%u ppm age=%lu ms",analog_values.co_ppm,(unsigned long)(now-analog_time));
        else LiveLine(DIAG_WARN,"CO: %s",board_analog_calibration.calibrated?"INVALID / STALE":"NOT CALIBRATED");
        BurnerAppSnapshot b;BurnerApp_GetSnapshot(&b);
        LiveLine(DIAG_INFO,"CO limits accept/run/trip=%u/%u/%u",b.co_accept_ppm,b.co_limit_ppm,b.co_trip_ppm);
#endif
        for(unsigned i=0;i<2;++i) {
            const MAX6675_Data_t *d=&temperatures.sensor[i];char temperature[24];Temperature(temperature,sizeof(temperature),d);
            LiveLine(d->valid?DIAG_INFO:DIAG_WARN,"TC%u %s: %s",i+1,i?"sample cooler":"flame",temperature);
            if(d->has_valid_sample)LiveLine(d->valid?DIAG_INFO:DIAG_WARN,"  valid=%u age=%lu ms stale=%u",d->valid,(unsigned long)(now-d->last_valid_ms),d->stale);
            else LiveLine(DIAG_WARN,"  No valid sample received");
            LiveLine(DIAG_INFO,"  SPI errors=%lu OPEN errors=%lu",(unsigned long)d->spi_error_count,(unsigned long)d->open_error_count);
        }
        if(co2->valid)LiveLine(DIAG_INFO,"CO2=%u ppm age=%lu ms",co2->ppm,(unsigned long)(now-co2->last_valid_ms));
        else LiveLine(DIAG_WARN,"CO2: %s",co2->link_stale?"NO LINK":"INVALID / WARMUP");
#if defined(FW_FINAL_APPLICATION)
        BoardActuationSnapshot act;BoardActuation_Get(&act);
        LiveLine(DIAG_INFO,"Hall=%s REF=%u EXH=%u GREEN=%u",(GPIOB->IDR&(1U<<12))?"INACTIVE":"ACTIVE",act.referenced,act.bypass_proven,act.process_proven);
        LiveLine(act.fan_sync?DIAG_INFO:DIAG_WARN,"Fan sync: %s",act.fan_sync?"VALID":"MISSING");
        if(act.fan_sync_age_ms!=UINT32_MAX)LiveLine(DIAG_INFO,"ZC period=%lu us last pulse=%lu ms ago",(unsigned long)act.fan_period_us,(unsigned long)act.fan_sync_age_ms);
        else LiveLine(DIAG_WARN,"ZC: no captured pulses");
        if(!board_bus_config.enabled)LiveLine(DIAG_INFO,"Daylight: RS485 DISABLED");
        else if(!serial_bus.lux_valid)LiveLine(DIAG_WARN,"Daylight: INVALID / STALE");
        else LiveLine(DIAG_INFO,"Daylight=%s lux=%lu.%02lu age=%lu ms",serial_bus.daylight?"DAY":"NIGHT",
            (unsigned long)(serial_bus.lux_centi/100U),(unsigned long)(serial_bus.lux_centi%100U),(unsigned long)(now-serial_bus.lux_ms));
#endif
    } else if(diag.section==HMI_DIAG_COMMS) {
        if(co2->valid)LiveLine(DIAG_INFO,"CO2 link: VALID, %u ppm",co2->ppm);
        else LiveLine(DIAG_WARN,"CO2 link: %s",co2->link_stale?"STALE / NO DATA":"WARMUP / INVALID");
        LiveLine(radio.ready?DIAG_INFO:DIAG_WARN,"Radio: %s",radio.ready?"READY":"NOT READY");
        if(co2->has_valid_sample)LiveLine(DIAG_INFO,"CO2 sample age=%lu ms",(unsigned long)(now-co2->last_valid_ms));
        LiveLine(DIAG_INFO,"RX=%lu dropped=%lu SPI errors=%lu",(unsigned long)radio.stats.rx_packets,(unsigned long)radio.stats.rx_dropped,(unsigned long)radio.stats.spi_errors);
#if defined(FW_FINAL_APPLICATION)
        LiveLine(DIAG_INFO,"RS485: %s",!board_bus_config.enabled?"DISABLED":serial_bus.configured?"CONFIGURED":"NOT READY");
        if(board_bus_config.enabled) {
            LiveLine(serial_bus.plc_valid?DIAG_INFO:DIAG_WARN,"PLC=%s DAYLIGHT=%s",serial_bus.plc_valid?"VALID":"NO DATA",serial_bus.lux_valid?"VALID":"NO DATA");
            LiveLine(DIAG_INFO,"Timeout=%lu CRC=%lu protocol=%lu",(unsigned long)serial_bus.timeouts,(unsigned long)serial_bus.crc_errors,(unsigned long)serial_bus.protocol_errors);
            LiveLine(DIAG_INFO,"TX=%lu replies=%lu retries=%lu",(unsigned long)serial_bus.tx_count,(unsigned long)serial_bus.completed,(unsigned long)serial_bus.retries);
        }
#endif
        LiveLine(DIAG_INFO,"UART %lu %s %s RX-DMA=%u",(unsigned long)huart1.Init.BaudRate,
            huart1.Init.Parity==UART_PARITY_NONE?"8N":huart1.Init.Parity==UART_PARITY_EVEN?"8E":"8O",
            huart1.Init.StopBits==UART_STOPBITS_2?"2":"1",io.uart_started);
        LiveLine(DIAG_INFO,"UART bytes=%lu errors=%lu",(unsigned long)io.rx_bytes,(unsigned long)io.rx_errors);
        LiveLine(DIAG_INFO,"Radio recoveries=%lu config errors=%lu",(unsigned long)radio.stats.recoveries,(unsigned long)radio.stats.config_errors);
    } else if(diag.section==HMI_DIAG_SUMMARY) {
#if defined(FW_FINAL_APPLICATION)
        BurnerAppSnapshot b;BurnerApp_GetSnapshot(&b);
        BoardActuationSnapshot act;BoardActuation_Get(&act);
        LiveLine(b.fault?DIAG_ERROR:DIAG_INFO,"Fault: %s%s%s",Burner_FaultText(b.fault),b.fault?" / ":"",b.fault?DiagSource(b.fault_source):"");
        LiveLine(DIAG_INFO,"Stage: %s / %s",Burner_StateText(b.state),b.mode==BURNER_AUTO_MODE?"AUTO":"MANUAL");
        if(b.fault)LiveLine(DIAG_ERROR,"Check: %s",FaultAction(b.fault));
        if(act.inhibited) {
            LiveLine(DIAG_WARN,"Inhibit: %s",BoardActuation_InhibitText(act.inhibit_flags));
            if(act.inhibit_flags&ACT_BLOCK_FAULT)LiveLine(DIAG_WARN,"Action: inspect damper / Outputs details");
            else if(act.inhibit_flags&ACT_BLOCK_REFERENCE)LiveLine(DIAG_WARN,"Action: confirm actual EXHAUST in Service");
            else if(act.inhibit_flags&ACT_BLOCK_SYNC)LiveLine(DIAG_WARN,"Action: check zero-cross input PA9");
            else if(act.inhibit_flags&ACT_BLOCK_CALIBRATION)LiveLine(DIAG_WARN,"Action: check fan CCR table");
        }
        LiveLine(DIAG_INFO,"Next: %s",act.inhibited&&!b.fault?"Resolve output inhibit shown above":StageWait(&b));
        if(b.timing.state_timer_active)LiveLine(DIAG_INFO,"Program timer left=%lu s (logical)",(unsigned long)((b.timing.state_remaining_ms+999U)/1000U));
        LiveLine(DIAG_INFO,"Fan req=%u CCR=%lu gate=%u",b.requested.combustion_fan,(unsigned long)TIM1->CCR1,(unsigned)((TIM1->BDTR&TIM_BDTR_MOE)!=0));
        LiveLine(DIAG_INFO,"REF=%u sync=%u table=%u inhibited=%u",act.referenced,act.fan_sync,act.fan_calibrated,act.inhibited);
        LiveLine(DIAG_INFO,"Gas req/sent=%u/%u Ign=%u/%u",b.requested.gas,(bus.latched>>7)&1U,b.requested.ignition,(bus.latched>>1)&1U);
        if(b.state==BURNER_RUNNING) {
            LiveLine(DIAG_INFO,"Sample: %s",Burner_SampleText(b.sample_state));
            if(b.timing.sample_timer_active)LiveLine(DIAG_INFO,"Sample timer/limit left=%lu s",(unsigned long)((b.timing.sample_remaining_ms+999U)/1000U));
        }
        char t1[24],t2[24];Temperature(t1,sizeof(t1),&temperatures.sensor[0]);Temperature(t2,sizeof(t2),&temperatures.sensor[1]);
        LiveLine(temperatures.sensor[0].valid?DIAG_INFO:DIAG_WARN,"TC1=%s TC2=%s",t1,t2);
        if(analog_values.co_valid)LiveLine(DIAG_INFO,"CO=%u ppm / accept<=%u",analog_values.co_ppm,b.co_accept_ppm);
        else LiveLine(DIAG_WARN,"CO: %s",board_analog_calibration.calibrated?"INVALID / STALE":"NOT CALIBRATED");
        LiveLine(DIAG_INFO,"Air command level=%u adjustments=%u",b.air_percent,b.air_adjustments);
        LiveLine(DIAG_WARN,"Airflow is inferred, NOT measured RPM/flow");
        if(b.state==BURNER_IDLE) {
            LiveLine(DIAG_INFO,"New START warnings (override policy):");
            for(unsigned bit=0;bit<14U;++bit)if(b.start_blockers&(1UL<<bit))
                LiveLine(DIAG_WARN,"%s",Burner_StartBlockText(1UL<<bit));
        }
#else
        LiveLine(DIAG_WARN,"Monitor only; burner control unavailable");
#endif
    } else if(diag.section==HMI_DIAG_SYSTEM) {
        if(hmi_monitor_crash.magic==0x484d4946U)LiveLine(DIAG_ERROR,"Retained CPU fault=%lu PC=%08lX",(unsigned long)hmi_monitor_crash.exception,(unsigned long)hmi_monitor_crash.pc);
        LiveLine(lcd.errors?DIAG_WARN:DIAG_INFO,"LCD errors=%lu stream error=%u",(unsigned long)lcd.errors,(unsigned)stream.error);
        LiveLine(assets_ok?DIAG_INFO:DIAG_ERROR,"Assets CRC: %s",assets_ok?"OK":"FAIL");
        LiveLine(DIAG_INFO,"SPI2 errors=%lu log overwritten=%lu",(unsigned long)bus.errors,(unsigned long)DiagLog_Overwritten());

        LiveLine(DIAG_INFO,"Uptime=%lu ms CPU=%lu Hz",(unsigned long)now,(unsigned long)SystemCoreClock);
        LiveLine(DIAG_INFO,"CPU code: INTERNAL FLASH 0x08000000");
        uint32_t lcd_divider=2UL<<((hspi1.Instance->CR1&SPI_CR1_BR)>>3U);
        LiveLine(DIAG_INFO,"LCD SPI1=%lu Hz /%lu; DMA2 Stream2",
            (unsigned long)(HAL_RCC_GetPCLK2Freq()/lcd_divider),(unsigned long)lcd_divider);
        LiveLine(DIAG_INFO,"Render buffers=%u bytes; tile=480x%u",
            (unsigned)(sizeof(first)+sizeof(second)),(unsigned)HMI_DISPLAY_TILE_ROWS);
        LiveLine(DIAG_INFO,"Display job max-step=%lu us",(unsigned long)(render_cycles/(SystemCoreClock/1000000U)));
        LiveLine(DIAG_INFO,"LCD job last=%lu max=%lu full=%lu ms",
            (unsigned long)last_display_job_ms,(unsigned long)max_display_job_ms,(unsigned long)last_full_page_ms);
        LiveLine(DIAG_INFO,"Live capture max=%lu us",(unsigned long)(capture_cycles/(SystemCoreClock/1000000U)));
        LiveLine(DIAG_INFO,"Main max interval=%lu us (includes WFI)",(unsigned long)(loop_cycles/(SystemCoreClock/1000000U)));
        LiveLine(DIAG_INFO,"LCD errors=%lu stream-error=%u",(unsigned long)lcd.errors,(unsigned)stream.error);
        LiveLine(assets_ok?DIAG_INFO:DIAG_ERROR,"W25Q16 CRC=%s JEDEC=%06lX",assets_ok?"OK":"FAIL",(unsigned long)assets_jedec);
        LiveLine(DIAG_INFO,"QSPI lanes=%lu booterror=%lu",(unsigned long)assets_read_lanes,(unsigned long)assets_boot_error);
        LiveLine(DIAG_INFO,"QSPI clock=%lu Hz",(unsigned long)(SystemCoreClock/(1U+((QUADSPI->CR>>24)&255U))));
#if defined(FW_FINAL_APPLICATION)
        LiveLine(asset_dma.failed?DIAG_ERROR:DIAG_INFO,"Asset DMA2/S1 active=%u done=%lu errors=%lu",
            asset_dma.busy,(unsigned long)asset_dma.completed,(unsigned long)asset_dma.errors);
        LiveLine(DIAG_INFO,"Asset DMA bytes=%lu; fonts cached=%u/%lu",
            (unsigned long)asset_dma.bytes,page_dma.font_count,(unsigned long)page_dma.font_used);
        LiveLine(DIAG_INFO,"Asset trigger TIM8 internal; no render in IRQ");
        LiveLine(asset_dma_fallback?DIAG_ERROR:DIAG_INFO,"Asset fallback=%u flags=%08lX NDTR=%lu FIFO=%lu",
            asset_dma_fallback,(unsigned long)asset_dma.last_flags,(unsigned long)asset_dma.last_ndtr,
            (unsigned long)asset_dma.fifo_events);
#endif
        LiveLine(DIAG_INFO,"Key sampling gaps=%lu drops=%lu",(unsigned long)keys.gaps,(unsigned long)keys.dropped);
        LiveLine(DIAG_INFO,"Log entries=%u overwritten=%lu",DiagLog_Count(),(unsigned long)DiagLog_Overwritten());
        LiveLine(DIAG_INFO,"595 latch count=%lu bus errors=%lu",(unsigned long)bus.latches,(unsigned long)bus.errors);
        LiveLine(DIAG_INFO,"ADC sequence=%lu errors=%lu",(unsigned long)io.adc_sequence,(unsigned long)io.adc_errors);
#if defined(FW_FINAL_APPLICATION)
        LiveLine(DIAG_WARN,"Pending integration items=%u",board_pending_count);
        for(unsigned i=0;i<board_pending_count;++i)LiveLine(DIAG_WARN,"%s",board_pending_items[i]);
#endif
        LiveLine(DIAG_INFO,"Raw TC1=%04X status=%u TC2=%04X status=%u",temperatures.sensor[0].raw,(unsigned)temperatures.sensor[0].last_status,temperatures.sensor[1].raw,(unsigned)temperatures.sensor[1].last_status);
        LiveLine(DIAG_INFO,"595 queued=%02X sent=%02X state=%u",bus.shadow,bus.latched,bus.state);
        LiveLine(DIAG_INFO,"NRF status=%02X channel=%u",radio.status,NRF24_CHANNEL);
        HexRows("NRF",last_nrf,nrf_frames?32:0);
        HexRows("UART",io.rx,io.rx_length);
        if(hmi_monitor_crash.magic==0x484d4946U) {
            LiveLine(DIAG_ERROR,"Retained CPU fault: exception=%lu",(unsigned long)hmi_monitor_crash.exception);
            LiveLine(DIAG_ERROR,"PC=%08lX CFSR=%08lX",(unsigned long)hmi_monitor_crash.pc,(unsigned long)hmi_monitor_crash.cfsr);
        }
    }
    if(diag.scroll>=live_index && diag.scroll) {diag.scroll=live_index?(uint16_t)(live_index-1U):0;++diag.revision;}
    uint32_t elapsed=DWT->CYCCNT-cycle_start;if(elapsed>capture_cycles)capture_cycles=elapsed;
}
#if defined(FW_FINAL_APPLICATION)
static const char *SourceName(BurnerSource source)
{return source==BURNER_PLC?"Master controller command":source==BURNER_AUTO?"Automatic control command":"Display command";}
static void CapturePage(const CO2_Snapshot *co2)
{
    memset(&page_view,0,sizeof(page_view));page_view.page=active_page;page_view.font=font;page_view.focus=route_focus;
    if(!strcmp(active_page->id,"home")) {
        HmiBinding selected=navigation.focus==0?HMI_BIND_RUN_STATE:HMI_BIND_STOP_STATE;
        page_view.focus=-1;
        for(unsigned i=0;i<active_page->count;i++)if(active_page->widgets[i].binding==selected) {page_view.focus=(int16_t)i;break;}
    }
    Temperature(page_view.values[HMI_BIND_TC1],48,&temperatures.sensor[0]);
    Temperature(page_view.values[HMI_BIND_TC2],48,&temperatures.sensor[1]);
    BoardAnalogValues analog;BoardRuntime_GetAnalog(&analog,NULL);
    if(analog.co_valid)snprintf(page_view.values[HMI_BIND_CO],48,"%u ppm",analog.co_ppm);
    else snprintf(page_view.values[HMI_BIND_CO],48,"UNCAL / INVALID");
    if(co2->valid)snprintf(page_view.values[HMI_BIND_CO2],48,"%u ppm",co2->ppm);
    else snprintf(page_view.values[HMI_BIND_CO2],48,"%s",co2->link_stale?"NO LINK":"INVALID");
    BurnerAppSnapshot b;BurnerApp_GetSnapshot(&b);
    snprintf(page_view.values[HMI_BIND_STATE],48,"%s",b.actuators_inhibited?"INHIBITED":Burner_StateText(b.state));
    snprintf(page_view.values[HMI_BIND_MODE],48,"%s",b.mode==BURNER_AUTO_MODE?"AUTO":"MANUAL");
    snprintf(page_view.values[HMI_BIND_TARGET],48,"Target %u ppm",b.co2_target_ppm);
    snprintf(page_view.values[HMI_BIND_SOURCE],48,"%s | %s",b.configured?Burner_StateText(b.state):"CONFIG REQUIRED",SourceName(b.command_source));
    if(b.simulated)snprintf(page_view.values[HMI_BIND_SOURCE],48,b.output_test?"OUTPUT TEST: %s | NO FUEL":"DRY RUN: %s | OUTPUTS OFF",Burner_StateText(b.state));
    snprintf(page_view.values[HMI_BIND_FAULT],48,"%s",Burner_FaultText(b.fault));
    BoardActuationSnapshot act;BoardActuation_Get(&act);
    snprintf(page_view.values[HMI_BIND_DAMPER],48,"%s / %ld",act.fault?"FAULT":!act.referenced?"REF REQUIRED":act.moving?"MOVING":act.bypass_proven?"EXHAUST":"GREENHOUSE",(long)DamperStepper_GetPosition());
    snprintf(page_view.values[HMI_BIND_AIR],48,"%u%% CCR=%u %s",b.air_percent,act.fan_ccr,act.fan_calibrated?act.fan_sync?"SYNC":"NO ZC":"UNCAL");
    snprintf(page_view.values[HMI_BIND_SAMPLE],48,"%s",Burner_SampleText(b.sample_state));
    HmiHomeState home={.automatic=b.mode==BURNER_AUTO_MODE,.configured=b.configured,.inhibited=b.actuators_inhibited,
        .running=b.state==BURNER_RUNNING,.starting=b.state>=BURNER_PRECHECK&&b.state<=BURNER_IGNITION_2,
        .stopping=b.state==BURNER_SHUTDOWN||b.state==BURNER_POSTPURGE,.stopped_proven=b.stopped_proven,
        .faulted=b.fault!=BURNER_NO_FAULT,.process_proven=b.process_proven,.bypass_proven=b.bypass_proven,
        .co_valid=analog.co_valid,.co_calibrated=board_analog_calibration.calibrated,.co_ppm=analog.co_ppm,.co_limit_ppm=b.co_limit_ppm,
        .co_accept_ppm=board_burner_profile.co_accept_ppm,.co2_valid=co2->valid,.co2_link_stale=co2->link_stale,
        .co2_ppm=co2->ppm,.target_ppm=b.co2_target_ppm};
    HmiHome_Apply(&page_view,&home);
#if HMI_DISPLAY_TEST_TARGET_BLINK
    if((HAL_GetTick()/HMI_DISPLAY_TEST_BLINK_HALF_MS)&1U) {
        for(unsigned i=0;i<active_page->count;++i) {
            const HmiWidget *w=&active_page->widgets[i];
            if(w->kind==HMI_PAGE_IMAGE2&&w->id&&!strcmp(w->id,"target_panel")) {
                uint16_t colors[4];memcpy(colors,w->colors,sizeof(colors));
                colors[1]=Hmi_RGB565(255,0,0); /* border index only; preserve background/title/icon */
                (void)HmiPages_SetPalette(&page_view,w->id,colors);break;
            }
        }
    }
#endif
    if(b.fault!=BURNER_NO_FAULT) {
        if(b.simulated)snprintf(page_view.values[HMI_BIND_SOURCE],48,b.output_test?"OUT TEST: FAULT %s":"DRY RUN: FAULT %s",Burner_FaultText(b.fault));
        else snprintf(page_view.values[HMI_BIND_SOURCE],48,"FAULT %s | %s",Burner_FaultText(b.fault),b.fault_source==BURNER_PLC?"PLC":b.fault_source==BURNER_AUTO?"AUTO":"HMI");
    }
    if(home_command_visible&&HAL_GetTick()-home_command_ms<4000U) {
        if(b.simulated)snprintf(page_view.values[HMI_BIND_SOURCE],48,b.output_test?"OUT TEST: %.37s":"DRY RUN: %.38s",home_command_message);
        else snprintf(page_view.values[HMI_BIND_SOURCE],48,"%s",home_command_message);
    }
}
static void CaptureMenu(void)
{
    memset(&menu_view,0,sizeof(menu_view));menu_view.font=HmiPagesDma_CachedFont(&page_dma,font);
    if(!menu_view.font)menu_view.font=&monitor_fallback_font;
    menu_view.width=480;menu_view.height=320;
    menu_view.background=0x0841;menu_view.line_count=10;menu_view.columns=46;
    bool service=navigation.page==UI_PAGE_SERVICE;
    bool fan=navigation.page==UI_PAGE_FAN;
    snprintf(menu_view.header,64,"%s",fan?"FAN CCR / RPM MEASUREMENT":service?"SERVICE / REFERENCE":"SETTINGS / CONTROL");
    snprintf(menu_view.status,64,"%s",menu_message[0]?menu_message:service?"Confirm EXHAUST only at physical exhaust endpoint":"Values saved in RAM; safety profile separate");
    snprintf(menu_view.footer,64,"UP/DN select | OK apply | Cancel back");
    BurnerAppSnapshot b;BurnerApp_GetSnapshot(&b);
    if(fan) {
        BoardActuationSnapshot act;BoardActuation_Get(&act);
        unsigned ccr=navigation.editing&&navigation.focus==0U?navigation.fan_draft:navigation.fan_ccr;
        unsigned low=navigation.editing&&navigation.focus==1U?navigation.fan_draft:navigation.fan_min;
        unsigned high=navigation.editing&&navigation.focus==2U?navigation.fan_draft:navigation.fan_max;
        snprintf(menu_view.lines[0],64,"%c Selected CCR: %u",navigation.focus==0?'>':' ',ccr);
        snprintf(menu_view.lines[1],64,"%c Minimum CCR: %u",navigation.focus==1?'>':' ',low);
        snprintf(menu_view.lines[2],64,"%c Maximum CCR: %u",navigation.focus==2?'>':' ',high);
        snprintf(menu_view.lines[3],64,"%c STOP FAN",navigation.focus==3?'>':' ');
        snprintf(menu_view.lines[4],64,"%c Return to Settings",navigation.focus==4?'>':' ');
        snprintf(menu_view.lines[6],64,"Applied CCR=%u / %s / ZC=%u",act.fan_ccr,act.fan_tuning?"TEST ACTIVE":"OFF",act.fan_sync);
        snprintf(menu_view.lines[7],64,"RPM: external measurement; report with CCR");
        snprintf(menu_view.lines[8],64,"UP +50 / DOWN -50 while editing");
        snprintf(menu_view.lines[9],64,"1000=OFF; limits in RAM; test max 120s");
        snprintf(menu_view.status,64,"%s",menu_message[0]?menu_message:"Fan only / IDLE / relays OFF / limits 0..1000");
        snprintf(menu_view.footer,64,"OK edit/apply | Cancel discard/back");
    } else if(service) {
        const char *labels[]={"Motor +160 pulses (exhaust direction)","Motor -160 pulses (greenhouse direction)","STOP motor / clear reference","Test ALL relays 3s (OK stops)","Confirm physically at EXHAUST","Acknowledge fault","Return to Home"};
        for(unsigned i=0;i<7;++i)snprintf(menu_view.lines[i],64,"%c %s",navigation.focus==i?'>':' ',labels[i]);
        BoardActuationSnapshot act;BoardActuation_Get(&act);
        snprintf(menu_view.lines[8],64,"POS=%ld REF=%u HALL=%u MOVE=%u FLT=%u",(long)DamperStepper_GetPosition(),act.referenced,(unsigned)(HAL_GPIO_ReadPin(HAL1_OPEN_GPIO_Port,HAL1_OPEN_Pin)==GPIO_PIN_RESET),act.moving,act.fault);
        snprintf(menu_view.lines[9],64,"ZC=%u CCR=%u / %s",act.fan_sync,act.fan_ccr,b.configured?"PROFILE OK":"CONFIG REQUIRED");
    } else {
        bool auto_mode=navigation.editing&&navigation.focus==0?navigation.draft_auto:b.mode==BURNER_AUTO_MODE;
        unsigned co2=navigation.editing&&navigation.focus==1?navigation.draft_co2:b.co2_target_ppm;
        snprintf(menu_view.lines[0],64,"%c Mode: %s",navigation.focus==0?'>':' ',auto_mode?"AUTO":"MANUAL");
        snprintf(menu_view.lines[1],64,"%c CO2 target: %u ppm",navigation.focus==1?'>':' ',co2);
        const char *labels[]={"Request START","Request STOP","Hardware service","Fan CCR / RPM tuning","Return to Home"};
        for(unsigned i=0;i<5;++i)snprintf(menu_view.lines[i+2],64,"%c %s",navigation.focus==i+2?'>':' ',labels[i]);
        snprintf(menu_view.lines[8],64,"%s",navigation.editing?"EDIT: OK save / Cancel discard":"Hold OK 5s: diagnostics");
        snprintf(menu_view.lines[9],64,"%s / %s",b.configured?"PROFILE OK":"CONFIG REQUIRED",Burner_StateText(b.state));
    }
    for(unsigned i=0;i<menu_view.line_count;++i)menu_view.levels[i]=i==navigation.focus?(navigation.editing?DIAG_WARN:DIAG_INFO):DIAG_DEBUG;
}
#endif
static void Display(uint32_t now,const CO2_Snapshot *co2)
{
    uint32_t cycles=DWT->CYCCNT;
    uint32_t completed_before=stream.completed_jobs;
    HmiResult result=HmiStream_Process(&stream);
    uint32_t elapsed=DWT->CYCCNT-cycles;if(elapsed>render_cycles)render_cycles=elapsed;
    if(stream.completed_jobs!=completed_before) {
        last_display_job_ms=HAL_GetTick()-display_job_start;
        if(last_display_job_ms>max_display_job_ms)max_display_job_ms=last_display_job_ms;
        if(display_job_full)last_full_page_ms=last_display_job_ms;
    }
#if defined(FW_FINAL_APPLICATION)
    if(result!=HMI_OK&&result!=HMI_BUSY&&asset_dma.failed&&!asset_dma_fallback) {
        /* The graphics read failed, not necessarily the LCD transport. Finish
         * the hardware-owned LCD tile before releasing any RGB/snapshot buffer.
         * Keep DIAG operational with the internal font and plain-text Home. */
        HmiResult drained=stream.transport.poll(stream.transport.context);
        if(drained==HMI_OK&&HmiAssetDma_Quiescent(&asset_dma)) {
            asset_dma_fallback=true;font=&monitor_fallback_font;
            DiagLog_Write(now,DIAG_ERROR,"ASSET","DMA failed flags=%08lX NDTR=%lu; text fallback",
                (unsigned long)asset_dma.last_flags,(unsigned long)asset_dma.last_ndtr);
            if(HmiStream_Init(&stream,HmiSt7796_Transport(&lcd),480,320,first,second,HMI_DISPLAY_BUFFER_PIXELS)!=HMI_OK)Error_Handler();
            page_changed=true;painted=false;
        }
        return;
    }
#endif
    if(result!=HMI_OK || stream.busy)return;
    bool need=page_changed || render_revision!=diag.revision || now-last_capture>=100U;
    if(!need)return;
    last_capture=now;
    HmiRect dirty={0,0,480,320};
    bool new_page=!painted||render_diag!=diag.open||page_changed;
#if defined(FW_FINAL_APPLICATION)
    uint8_t kind=diag.open?3U:navigation.page==UI_PAGE_HOME?0U:navigation.page==UI_PAGE_SERVICE?2U:1U;
    new_page=new_page||painted_kind!=kind;
#endif
    if(diag.open) {
        char status[HMI_DIAG_COLUMNS];
#if !defined(FW_FINAL_APPLICATION)
        HC595_Diagnostics bus;HC595_GetDiagnostics(&bus);
#endif
#if defined(FW_FINAL_APPLICATION)
        BurnerAppSnapshot ds;BurnerApp_GetSnapshot(&ds);
        BoardActuationSnapshot da;BoardActuation_Get(&da);
        snprintf(status,sizeof(status),"%s | %s | %s",Burner_StateText(ds.state),Burner_FaultText(ds.fault),da.inhibited?"OUTPUT INHIBITED":"OUTPUT PERMITTED");
#else
        snprintf(status,sizeof(status),"MONITOR | SPI2 errors=%lu",(unsigned long)bus.errors);
#endif
        const HmiFont *diag_font=font;
#if defined(FW_FINAL_APPLICATION)
        diag_font=HmiPagesDma_CachedFont(&page_dma,font);
        if(!diag_font)diag_font=&monitor_fallback_font;
#endif
        if(HmiDiag_Capture(&diag,&view,diag_font,480,320,status)!=HMI_OK)return;
        if(diag.section!=HMI_DIAG_LOG)CaptureLive(now,co2);
        render_revision=diag.revision;
        if(!new_page && !ChangedRect(&dirty))return;
        result=HmiStream_Submit(&stream,dirty,HmiDiag_Compose,&view);
        if(result==HMI_OK)previous=view;
    }
#if defined(FW_FINAL_APPLICATION)
    else if(navigation.page!=UI_PAGE_HOME) {
        CaptureMenu();
        if(!new_page&&!memcmp(&menu_view,&previous,sizeof(menu_view)))return;
        result=HmiStream_Submit(&stream,dirty,HmiDiag_Compose,&menu_view);
        if(result==HMI_OK)previous=menu_view;
    } else if(assets_ok&&active_page&&!asset_dma_fallback) {
        HmiPageView old=page_view;CapturePage(co2);
        if(!new_page) {
            bool changed=false;int32_t left=480,top=320,right=0,bottom=0;
            for(unsigned i=0;i<active_page->count;++i) {
                const HmiWidget *w=&active_page->widgets[i];
                if((unsigned)w->binding>=HMI_BIND_COUNT){assets_ok=false;page_changed=true;return;}
                if(!HmiPages_WidgetChanged(&old,&page_view,i))continue;
                changed=true;if(w->bounds.x<left)left=w->bounds.x;if(w->bounds.y<top)top=w->bounds.y;
                if(w->bounds.x+w->bounds.w>right)right=w->bounds.x+w->bounds.w;
                if(w->bounds.y+w->bounds.h>bottom)bottom=w->bounds.y+w->bounds.h;
            }
            if(!changed)return;
            dirty=(HmiRect){left,top,(uint16_t)(right-left),(uint16_t)(bottom-top)};
        }
        result=HmiStream_Submit(&stream,dirty,HmiPagesDma_Compose,&page_dma);
    }
#endif
    else {
        CaptureHome(co2);
        if(!new_page) {
            unsigned first_changed=9,last_changed=0;
            for(unsigned i=0;i<9;++i)if(strcmp(home_lines[i],previous_home[i])) {
                if(first_changed==9)first_changed=i;
                last_changed=i;
            }
            if(first_changed==9)return;
            dirty=(HmiRect){0,42+(int32_t)first_changed*24,480,(uint16_t)((last_changed-first_changed)*24+font->height)};
        }
        result=HmiStream_Submit(&stream,dirty,Home,NULL);
        if(result==HMI_OK)memcpy(previous_home,home_lines,sizeof(home_lines));
    }
    if(result==HMI_OK) {
        display_job_start=HAL_GetTick();display_job_full=dirty.w==480U&&dirty.h==320U;
        painted=true;render_diag=diag.open;page_changed=false;render_revision=diag.revision;
    }
#if defined(FW_FINAL_APPLICATION)
    if(result==HMI_OK)painted_kind=kind;
#endif
}
static void Clock(void)
{
    RCC_OscInitTypeDef osc={0};RCC_ClkInitTypeDef clocks={0};
    __HAL_RCC_PWR_CLK_ENABLE();__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    osc.OscillatorType=RCC_OSCILLATORTYPE_HSE;osc.HSEState=RCC_HSE_ON;
    osc.PLL.PLLState=RCC_PLL_ON;osc.PLL.PLLSource=RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM=6;osc.PLL.PLLN=64;osc.PLL.PLLP=RCC_PLLP_DIV2;osc.PLL.PLLQ=2;osc.PLL.PLLR=2;
    if(HAL_RCC_OscConfig(&osc)!=HAL_OK)Error_Handler();
    clocks.ClockType=RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    clocks.SYSCLKSource=RCC_SYSCLKSOURCE_PLLCLK;clocks.AHBCLKDivider=RCC_SYSCLK_DIV1;
    clocks.APB1CLKDivider=RCC_HCLK_DIV2;clocks.APB2CLKDivider=RCC_HCLK_DIV1;
    if(HAL_RCC_ClockConfig(&clocks,FLASH_LATENCY_1)!=HAL_OK)Error_Handler();
}
int main(void)
{
    HAL_Init();Clock();DiagLog_Init();
    MX_GPIO_Init();MX_DMA_Init();MX_SPI1_Init();MX_SPI2_Init();MX_SPI5_Init();
    MX_USART1_UART_Init();MX_ADC1_Init();MX_TIM1_Init();MX_TIM6_Init();
    KeyInput_Init(&keys,HAL_GetTick());HmiDiag_Init(&diag);keys_ready=true;
    App_Status_t app=App_Init();
#if defined(FW_FINAL_APPLICATION)
    huart1.Init.BaudRate=board_bus_config.baud;
    huart1.Init.Parity=board_bus_config.parity==1U?UART_PARITY_EVEN:board_bus_config.parity==2U?UART_PARITY_ODD:UART_PARITY_NONE;
    huart1.Init.WordLength=board_bus_config.parity?UART_WORDLENGTH_9B:UART_WORDLENGTH_8B;
    huart1.Init.StopBits=board_bus_config.stop_bits==2U?UART_STOPBITS_2:UART_STOPBITS_1;
    if(HAL_UART_Init(&huart1)!=HAL_OK)Error_Handler();
#endif
    HmiMonitorIo_Init();
    /* PA8 remains TIM1_CH1 PWM2. CCR1=1000 keeps it LOW at boot;
     * PA9 resets TIM1 directly and the PB13 buzzer owns TIM4 independently. */
    (void)HC595_ClearAll();
    assets_ok=InternalAssets_Init()==HAL_OK;font=assets_ok?&monitor_font:&monitor_fallback_font;
#if defined(FW_FINAL_APPLICATION)
    UiNavigation_Init(&navigation);BurnerApp_Init();BoardBus_Init();
    if(assets_ok)active_page=HmiPages_Find(hmi_pages,hmi_page_count,"home");
    if(assets_ok&&(HmiAssetDma_Init(&asset_dma)!=HMI_OK||
        HmiPagesDma_Init(&page_dma,HmiAssetDma_Io(&asset_dma),&page_view)!=HMI_OK))Error_Handler();
#endif
    radio_ok=NRF24_BoardInit(&radio,NRF24_RECEIVER);
    DiagLog_Write(HAL_GetTick(),DIAG_INFO,"BOOT","New HMI; CPU internal; LCD 16MHz; App init=%u",(unsigned)app);
    DiagLog_Write(HAL_GetTick(),assets_ok?DIAG_INFO:DIAG_ERROR,"ASSET","CRC=%s boot-error=%lu",assets_ok?"OK":"FAILED",(unsigned long)assets_boot_error);
    DiagLog_Write(HAL_GetTick(),DIAG_INFO,"KEY","Active LOW: OK PC14 5s; Cancel PC13 exits");
    DiagLog_Write(HAL_GetTick(),DIAG_INFO,"MAP","TC2 sample cooler; Q5 distribution; one shared stepper");
    DiagLog_Write(HAL_GetTick(),DIAG_WARN,"PROCESS","Gas OFF; CO uncalibrated; burner uncommissioned");
    if(HmiSt7796_Init(&lcd)!=HMI_OK || HmiStream_Init(&stream,HmiSt7796_Transport(&lcd),
        480,320,first,second,HMI_DISPLAY_BUFFER_PIXELS)!=HMI_OK)Error_Handler();
    CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;
    HAL_NVIC_SetPriority(SysTick_IRQn,1,0);
    HAL_NVIC_SetPriority(DMA2_Stream2_IRQn,7,0);HAL_NVIC_SetPriority(SPI1_IRQn,7,0);
    HAL_NVIC_SetPriority(DMA1_Stream3_IRQn,1,0);HAL_NVIC_SetPriority(DMA1_Stream4_IRQn,1,0);
    HAL_NVIC_SetPriority(USART1_IRQn,2,0);
    while(1) {
        uint32_t cycle_start=DWT->CYCCNT;
        if(last_loop_cycles && cycle_start-last_loop_cycles>loop_cycles)loop_cycles=cycle_start-last_loop_cycles;
        last_loop_cycles=cycle_start;
        App_Process();HmiMonitorIo_Process();Keys();
        if(radio_ok) {
            NRF24_Process(&radio);uint8_t payload[32];
            for(unsigned i=0;i<3 && NRF24_Read(&radio,payload);++i) {
                memcpy(last_nrf,payload,sizeof(last_nrf));last_nrf_ms=HAL_GetTick();++nrf_frames;
                (void)CO2_Accept(&co2_cache,payload,HAL_GetTick());
            }
        }
        MAX6675_GetSnapshot(&temperatures);
        CO2_Snapshot co2=CO2_GetSnapshot(&co2_cache,HAL_GetTick());
#if defined(FW_FINAL_APPLICATION)
        BoardBus_Process();BurnerApp_Process(&temperatures,&co2);
#endif
        uint32_t now=HAL_GetTick();Health(now);Display(now,&co2);
        /* No display wait. DMA and SysTick wake the cooperative main loop. */
        __WFI();
    }
}
void Error_Handler(void) {__disable_irq();while(1) {}}
#endif
