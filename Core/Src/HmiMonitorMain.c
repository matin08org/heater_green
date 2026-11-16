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
static uint32_t last_capture,health_ms,last_tc[2],last_bus_errors;
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
    switch(action) {
    case NAV_SAVE_MODE:(void)BurnerApp_SetMode(navigation.draft_auto?BURNER_AUTO_MODE:BURNER_MANUAL_MODE);break;
    case NAV_SAVE_CO2:(void)BurnerApp_SetCO2Target(navigation.draft_co2);break;
    case NAV_START: {
        BurnerCommandResult result=BurnerApp_Start(BURNER_HMI);
        BurnerApp_GetSnapshot(&b);
        snprintf(menu_message,sizeof(menu_message),"START: %s",result==BURNER_COMMAND_ACCEPTED?"ACCEPTED":result==BURNER_COMMAND_BUSY?"BUSY / LOCKOUT":Burner_StartBlockText(b.start_blockers));
        snprintf(home_command_message,sizeof(home_command_message),"%.47s",menu_message);
        home_command_ms=HAL_GetTick();home_command_visible=true;break;
    }
    case NAV_STOP:
        BurnerApp_Stop(BURNER_HMI);
        snprintf(menu_message,sizeof(menu_message),"STOP: %s",b.state==BURNER_IDLE?"ALREADY IDLE":b.state==BURNER_LOCKOUT?"FAULT REMAINS":"SHUTDOWN REQUESTED");
        snprintf(home_command_message,sizeof(home_command_message),"%.47s",menu_message);
        home_command_ms=HAL_GetTick();home_command_visible=true;break;
    case NAV_STEP_PLUS:case NAV_STEP_MINUS: {
        int64_t endpoint=(int64_t)DamperStepper_GetPosition()+(action==NAV_STEP_PLUS?16:-16);
        HAL_StatusTypeDef result=HAL_BUSY;
        if(b.state==BURNER_IDLE&&!b.requested.gas&&!(HC595_GetShadow()&0x82U)&&!DamperStepper_IsRunning()&&endpoint>=INT32_MIN&&endpoint<=INT32_MAX) {
            result=BoardActuation_ManualStep(action==NAV_STEP_PLUS?16:-16);
        }
        snprintf(menu_message,sizeof(menu_message),"Motor: %s",result==HAL_OK?"16-pulse manual move; NOT HOMED":"refused busy / unsafe state");
        DiagLog_Write(HAL_GetTick(),result==HAL_OK?DIAG_INFO:DIAG_WARN,"SERVICE","Motor target=%ld result=%u",(long)endpoint,(unsigned)result);break;
    }
    case NAV_MOTOR_STOP:
        if(b.state==BURNER_IDLE) {BoardActuation_Abort();snprintf(menu_message,sizeof(menu_message),"Motor stopped; ref UNKNOWN");}
        else snprintf(menu_message,sizeof(menu_message),"Use device STOP while process owns motor");
        break;
    case NAV_CONFIRM_EXHAUST: {
        bool ok=b.state==BURNER_IDLE&&!b.requested.gas&&!(HC595_GetShadow()&0x82U)&&BoardActuation_ConfirmExhaust();
        snprintf(menu_message,sizeof(menu_message),"EXHAUST reference: %s",ok?"confirmed by operator":"refused; idle + Hall LOW required");
        DiagLog_Write(HAL_GetTick(),ok?DIAG_WARN:DIAG_ERROR,"DAMPER","Operator EXHAUST reference accepted=%u",ok);break;
    }
    case NAV_BEEP:(void)Buzzer_Play(2400,300);snprintf(menu_message,sizeof(menu_message),"Buzzer 2400Hz / 300ms");break;
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
            if(navigation.page==UI_PAGE_SERVICE)StopManualMotor();
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
    for(unsigned i=0;i<2;++i) {
        const MAX6675_Data_t *d=&temperatures.sensor[i];
        if(d->last_attempt_ms!=last_tc[i]) {
            last_tc[i]=d->last_attempt_ms;
            DiagLog_Write(now,d->valid?DIAG_DEBUG:DIAG_WARN,i?"TC2":"TC1",
                "raw=%04X q4=%u status=%u valid=%u old=%u age=%lu",d->raw,d->temperature_q4,
                (unsigned)d->last_status,d->valid,d->stale,
                (unsigned long)(d->has_valid_sample?now-d->last_valid_ms:0U));
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
static void CaptureLive(uint32_t now,const CO2_Snapshot *co2)
{
    uint32_t cycle_start=DWT->CYCCNT;
    static const char *names[]={"LOG","INPUTS","OUTPUTS","ADC","SENSORS","COMMS","PROCESS","SYSTEM"};
    memset(view.lines,0,sizeof(view.lines));memset(view.levels,0,sizeof(view.levels));live_index=0;
    snprintf(view.header,sizeof(view.header),"> DIAG / %s / LIVE",names[diag.section]);
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
    } else if(diag.section==HMI_DIAG_OUTPUTS) {
        LiveLine(DIAG_INFO,"595 REQUEST=%02X LATCHED=%02X",bus.shadow,bus.latched);
        const char *outputs[]={"Q0 MAX6675 SELECT","Q1 IGNITION","Q2 EXCHANGER FAN","Q3 SAMPLE VALVE 1",
            "Q4 SAMPLE VALVE 2","Q5 DISTRIBUTION FAN","Q6 SAMPLE COOLER FAN","Q7 GAS VALVE"};
        for(unsigned i=0;i<8;++i)LiveLine(DIAG_INFO,"%s req=%u latch=%u",outputs[i],(bus.shadow>>i)&1U,(bus.latched>>i)&1U);
        LiveLine(DIAG_WARN,"LATCHED is sent image, not relay feedback");
        LiveLine(DIAG_INFO,"PA8 FAN CCR=%lu pin=%u gate=%u",(unsigned long)TIM1->CCR1,(unsigned)((GPIOA->IDR>>8)&1U),(unsigned)((TIM1->BDTR&TIM_BDTR_MOE)!=0));
#if defined(FW_FINAL_APPLICATION)
        BoardActuationSnapshot act;BoardActuation_Get(&act);
        LiveLine(act.fault?DIAG_ERROR:DIAG_INFO,"ACT inhibit=%u fault=%u REF=%u MOVE=%u",act.inhibited,act.fault,act.referenced,act.moving);
        LiveLine(DIAG_INFO,"FAN calibrated=%u ZC sync=%u",act.fan_calibrated,act.fan_sync);
        LiveLine(DIAG_INFO,"DAMPER exhaust=%u greenhouse=%u moves=%lu",act.bypass_proven,act.process_proven,(unsigned long)act.moves);
#endif
        LiveLine(DIAG_INFO,"STEP PC8=%u DIR PC9=%u",(unsigned)((GPIOC->IDR>>8)&1U),(unsigned)((GPIOC->ODR>>9)&1U));
        LiveLine(DIAG_INFO,"EN PC6=%u PC7=%u [LOW=enable]",(unsigned)((GPIOC->ODR>>6)&1U),(unsigned)((GPIOC->ODR>>7)&1U));
        LiveLine(DIAG_INFO,"BUZZER PB13 TIM4 tone=%u",Buzzer_IsPlaying());
        LiveLine(DIAG_INFO,"NRF CE PA11=%u CSN PA15=%u",(unsigned)((GPIOA->ODR>>11)&1U),(unsigned)((GPIOA->ODR>>15)&1U));
        LiveLine(DIAG_INFO,"LCD CS PD2=%u DC PC12=%u RST PC10=%u",(unsigned)((GPIOD->ODR>>2)&1U),(unsigned)((GPIOC->ODR>>12)&1U),(unsigned)((GPIOC->ODR>>10)&1U));
        LiveLine(DIAG_INFO,"595 LATCH PA0=%u",(unsigned)(GPIOA->ODR&1U));
        LiveLine(DIAG_INFO,"Sample cooler fan: Q6 active HIGH");
        LiveLine(DIAG_INFO,"GPIOA ODR=%04lX GPIOB ODR=%04lX",GPIOA->ODR&0xffffUL,GPIOB->ODR&0xffffUL);
        LiveLine(DIAG_INFO,"GPIOC ODR=%04lX GPIOD ODR=%04lX",GPIOC->ODR&0xffffUL,GPIOD->ODR&0xffffUL);
    } else if(diag.section==HMI_DIAG_ADC) {
        bool fresh=io.adc_sequence && now-io.adc_ms<500U;
        uint16_t cal=*VREFINT_CAL_ADDR;
        HmiDiagAnalog analog;HmiDiag_Analog(io.raw,fresh,cal,*TEMPSENSOR_CAL1_ADDR,*TEMPSENSOR_CAL2_ADDR,&analog);
        uint32_t vdda=analog.vdda_mv;bool volts=analog.voltage_valid;
        LiveLine(fresh?DIAG_INFO:DIAG_ERROR,"ADC seq=%lu age=%lu ms %s",(unsigned long)io.adc_sequence,
            (unsigned long)(io.adc_sequence?now-io.adc_ms:0U),fresh?"VALID":"NO DATA / STALE");
        LiveLine(DIAG_INFO,"TIM5_CH1 internal PWM 20Hz -> ADC1 DMA S0");
        const char *labels[]={"PA2 ME2sig","PA3 ME2diff","PA4 ME2ntc","PA5 mixer NTC","VREFINT","CPU TEMP"};
        for(unsigned i=0;i<6;++i) {
            if(volts)LiveLine(DIAG_INFO,"%s raw=%u mV=%lu",labels[i],io.raw[i],(unsigned long)((uint32_t)io.raw[i]*vdda/4095U));
            else LiveLine(DIAG_WARN,"%s raw=%u mV=INVALID",labels[i],io.raw[i]);
        }
        LiveLine(volts?DIAG_INFO:DIAG_WARN,"VDDA=%lu mV VREFcal=%u",(unsigned long)vdda,cal);
        LiveLine(DIAG_INFO,"mV = raw * VDDA / 4095");
        LiveLine(DIAG_INFO,"ME2 differential raw = %ld",(long)((int32_t)io.raw[0]-io.raw[1]));
        if(volts)LiveLine(DIAG_INFO,"ME2 dV=%ld mV",(long)(((int32_t)io.raw[0]-io.raw[1])*(int32_t)vdda/4095));
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
        LiveLine(DIAG_INFO,"ADC errors=%lu started=%u",(unsigned long)io.adc_errors,io.adc_started);
        if(analog.cpu_temperature_valid) {
            LiveLine(DIAG_INFO,"CPU temp = %ld x 0.1 C",(long)analog.cpu_temperature_tenths);
        }
    } else if(diag.section==HMI_DIAG_SENSORS) {
        for(unsigned i=0;i<2;++i) {
            const MAX6675_Data_t *d=&temperatures.sensor[i];char temperature[24];Temperature(temperature,sizeof(temperature),d);
            LiveLine(d->valid?DIAG_INFO:DIAG_WARN,"TC%u %s: %s",i+1,i?"SAMPLE COOLER":"FLAME",temperature);
            LiveLine(DIAG_INFO," raw=%04X q4=%u status=%u",d->raw,d->temperature_q4,(unsigned)d->last_status);
            LiveLine(DIAG_INFO," has=%u valid=%u stale=%u age=%lu",d->has_valid_sample,d->valid,d->stale,(unsigned long)(d->has_valid_sample?now-d->last_valid_ms:0));
            LiveLine(DIAG_INFO," SPI errors=%lu OPEN errors=%lu",(unsigned long)d->spi_error_count,(unsigned long)d->open_error_count);
        }
        LiveLine(DIAG_INFO,"MAX6675: q4=(raw>>3)&0xFFF; C=q4/4");
        LiveLine(co2->valid?DIAG_INFO:DIAG_WARN,"CO2=%u ppm valid=%u age=%lu ms",co2->ppm,co2->valid,(unsigned long)(co2->has_valid_sample?now-co2->last_valid_ms:0));
        LiveLine(DIAG_WARN,"ME2 CO: UNCALIBRATED (see ADC)");
#if defined(FW_FINAL_APPLICATION)
        LiveLine(serial_bus.lux_valid?DIAG_INFO:DIAG_WARN,"RS485 BH1750 lux=%lu x0.01 valid=%u",(unsigned long)serial_bus.lux_centi,serial_bus.lux_valid);
        LiveLine(DIAG_INFO,"Daylight=%u LUX age=%lu ms",serial_bus.daylight,(unsigned long)(now-serial_bus.lux_ms));
#else
        LiveLine(DIAG_WARN,"Lux/daylight: NOT CONFIGURED");
#endif
        LiveLine(DIAG_INFO,"HALL raw PB12=%u; homing UNKNOWN",(unsigned)((GPIOB->IDR>>12)&1U));
        LiveLine(DIAG_INFO,"FLAME GPIO=%u; health UNPROVEN",(unsigned)((GPIOA->IDR>>1)&1U));
        LiveLine(DIAG_INFO,"ZC GPIO=%u; timing NOT VALIDATED",(unsigned)((GPIOA->IDR>>9)&1U));
    } else if(diag.section==HMI_DIAG_COMMS) {
        LiveLine(radio.ready?DIAG_INFO:DIAG_WARN,"NRF ready=%u detected=%u state=%u",radio.ready,radio.detected,(unsigned)radio.state);
        LiveLine(DIAG_INFO,"NRF ch=%u rate=1M STATUS=%02X",NRF24_CHANNEL,radio.status);
        LiveLine(DIAG_INFO,"RX=%lu dropped=%lu SPIerr=%lu",(unsigned long)radio.stats.rx_packets,(unsigned long)radio.stats.rx_dropped,(unsigned long)radio.stats.spi_errors);
        LiveLine(DIAG_INFO,"configErr=%lu recover=%lu",(unsigned long)radio.stats.config_errors,(unsigned long)radio.stats.recoveries);
        LiveLine(DIAG_INFO,"Last NRF frame count=%lu age=%lu",(unsigned long)nrf_frames,(unsigned long)(nrf_frames?now-last_nrf_ms:0));
        HexRows("NRF",last_nrf,nrf_frames?32:0);
        LiveLine(DIAG_INFO,"CO2 seq=%lu ppm=%u status=%u",(unsigned long)co2->last_sequence,co2->ppm,co2->last_status);
        LiveLine(DIAG_INFO,"accepted=%lu rejected=%lu duplicate=%lu",(unsigned long)co2->accepted,(unsigned long)co2->rejected,(unsigned long)co2->duplicates);
        LiveLine(io.uart_started?DIAG_INFO:DIAG_ERROR,"USART1 115200 8N1 RX-DMA=%u",io.uart_started);
        LiveLine(DIAG_INFO,"RX bytes=%lu chunks=%lu errors=%lu",(unsigned long)io.rx_bytes,(unsigned long)io.rx_chunks,(unsigned long)io.rx_errors);
        LiveLine(DIAG_INFO,"Last RX chunk age=%lu ms len=%u",(unsigned long)(io.rx_chunks?now-io.rx_ms:0),io.rx_length);
        HexRows("UART",io.rx,io.rx_length);
#if defined(FW_FINAL_APPLICATION)
        LiveLine(serial_bus.configured?DIAG_INFO:DIAG_WARN,"RS485 MASTER configured=%u slot=%u",serial_bus.configured,serial_bus.active_slot);
        LiveLine(DIAG_INFO,"TX=%lu OK=%lu timeout=%lu CRC=%lu",(unsigned long)serial_bus.tx_count,(unsigned long)serial_bus.completed,(unsigned long)serial_bus.timeouts,(unsigned long)serial_bus.crc_errors);
        LiveLine(DIAG_INFO,"PLC=%u LUX=%u exception=%u",serial_bus.plc_valid,serial_bus.lux_valid,serial_bus.last_exception);
#else
        LiveLine(DIAG_WARN,"RS485/Delta protocol+DE UNCONFIGURED");
        LiveLine(DIAG_INFO,"RX is raw UART data, not validated PLC");
#endif
        LiveLine(DIAG_INFO,"SPI1 LCD=%u SPI2 bus=%u SPI5 NRF=%u",(unsigned)hspi1.State,(unsigned)hspi2.State,(unsigned)hspi5.State);
    } else if(diag.section==HMI_DIAG_PROCESS) {
#if defined(FW_FINAL_APPLICATION)
        BurnerAppSnapshot b;BurnerApp_GetSnapshot(&b);
        LiveLine(b.actuators_inhibited?DIAG_WARN:DIAG_INFO,"%s %s inhibited=%u",b.output_test?"OUTPUT TEST":b.simulated?"DRY RUN":"BURNER",Burner_StateText(b.state),b.actuators_inhibited);
        LiveLine(DIAG_INFO,"Fault=%s source=%u",Burner_FaultText(b.fault),(unsigned)b.command_source);
        LiveLine(b.start_blockers?DIAG_WARN:DIAG_INFO,"New START blockers=%08lx",(unsigned long)b.start_blockers);
        for(unsigned bit=0;bit<14U;++bit)if(b.start_blockers&(1UL<<bit))
            LiveLine(DIAG_WARN,"%s",Burner_StartBlockText(1UL<<bit));
        LiveLine(DIAG_INFO,"Sampling=%s",Burner_SampleText(b.sample_state));
        LiveLine(DIAG_INFO,"Air request=%u%% adjustments=%u",b.air_percent,b.air_adjustments);
        LiveLine(DIAG_INFO,"Req GAS=%u IGN=%u FAN=%u",b.requested.gas,b.requested.ignition,b.requested.combustion_fan);
        LiveLine(DIAG_INFO,"Req V1=%u V2=%u SAMPLEFAN=%u",b.requested.sample_valve_1,b.requested.sample_valve_2,b.requested.cooler_fan);
        LiveLine(DIAG_INFO,"Req EXCHANGE=%u DISTRIB=%u DAMPER=%u",b.requested.exchanger_fan,b.requested.distribution_fan,(unsigned)b.requested.damper);
#else
        LiveLine(DIAG_WARN,"BURNER: INHIBITED / NOT COMMISSIONED");
        LiveLine(DIAG_INFO,"Ignition FSM not active in monitor profile");
        LiveLine(DIAG_INFO,"Sampling: IDLE; no sample valve request");
        LiveLine(DIAG_INFO,"Air adjustment: INACTIVE / UNCONFIG");
#endif
        LiveLine(DIAG_INFO,"Stepper running=%u position(cmd)=%ld",DamperStepper_IsRunning(),(long)DamperStepper_GetPosition());
#if defined(FW_FINAL_APPLICATION)
        BoardActuationSnapshot damper_status;BoardActuation_Get(&damper_status);
        LiveLine(damper_status.referenced?DIAG_INFO:DIAG_WARN,"Damper ref=%u EXH=%u GREEN=%u",damper_status.referenced,damper_status.bypass_proven,damper_status.process_proven);
#else
        LiveLine(DIAG_WARN,"Damper reference unavailable in monitor profile");
#endif
        LiveLine(DIAG_INFO,"TC1 flame; TC2 sample cooler");
        LiveLine(DIAG_INFO,"Q2 exchanger fan; Q5 distribution fan");
        LiveLine(DIAG_INFO,"Distribution requires confirmed GREEN endpoint");
        LiveLine(DIAG_INFO,"Relays requested=%02X latched=%02X",bus.shadow,bus.latched);
        LiveLine(DIAG_INFO,"MAX6675+595 bus=%u dirty=%u",bus.state,bus.dirty);
        LiveLine(DIAG_INFO,"Display busy=%u jobs=%lu tiles=%lu",stream.busy,(unsigned long)stream.completed_jobs,(unsigned long)stream.completed_tiles);
        LiveLine(DIAG_WARN,"No certified safety controller feedback");
    } else if(diag.section==HMI_DIAG_SYSTEM) {
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
        .co_valid=analog.co_valid,.co_calibrated=board_analog_calibration.calibrated,.co_ppm=analog.co_ppm,.co_limit_ppm=board_burner_profile.co_limit_ppm,
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
    snprintf(menu_view.header,64,"%s",service?"SERVICE / REFERENCE":"SETTINGS / CONTROL");
    snprintf(menu_view.status,64,"%s",menu_message[0]?menu_message:service?"Confirm EXHAUST only at physical exhaust endpoint":"Values saved in RAM; safety profile separate");
    snprintf(menu_view.footer,64,"UP/DN select | OK apply | Cancel back");
    BurnerAppSnapshot b;BurnerApp_GetSnapshot(&b);
    if(service) {
        const char *labels[]={"Motor +16 pulses (exhaust direction)","Motor -16 pulses (greenhouse direction)","STOP motor / clear reference","Test buzzer 300ms","Confirm physically at EXHAUST","Acknowledge fault","Return to Home"};
        for(unsigned i=0;i<7;++i)snprintf(menu_view.lines[i],64,"%c %s",navigation.focus==i?'>':' ',labels[i]);
        BoardActuationSnapshot act;BoardActuation_Get(&act);
        snprintf(menu_view.lines[8],64,"POS=%ld REF=%u HALL=%u MOVE=%u FLT=%u",(long)DamperStepper_GetPosition(),act.referenced,(unsigned)(HAL_GPIO_ReadPin(HAL1_OPEN_GPIO_Port,HAL1_OPEN_Pin)==GPIO_PIN_RESET),act.moving,act.fault);
        snprintf(menu_view.lines[9],64,"ZC=%u CCR=%u / %s",act.fan_sync,act.fan_ccr,b.configured?"PROFILE OK":"CONFIG REQUIRED");
    } else {
        bool auto_mode=navigation.editing&&navigation.focus==0?navigation.draft_auto:b.mode==BURNER_AUTO_MODE;
        unsigned co2=navigation.editing&&navigation.focus==1?navigation.draft_co2:b.co2_target_ppm;
        snprintf(menu_view.lines[0],64,"%c Mode: %s",navigation.focus==0?'>':' ',auto_mode?"AUTO":"MANUAL");
        snprintf(menu_view.lines[1],64,"%c CO2 target: %u ppm",navigation.focus==1?'>':' ',co2);
        const char *labels[]={"Request START","Request STOP","Hardware service","Return to Home"};
        for(unsigned i=0;i<4;++i)snprintf(menu_view.lines[i+2],64,"%c %s",navigation.focus==i+2?'>':' ',labels[i]);
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
        char status[HMI_DIAG_COLUMNS];HC595_Diagnostics bus;HC595_GetDiagnostics(&bus);
        snprintf(status,sizeof(status),"SPI2:%u Q:%02X/%02X NRF:%u H:%u F:%u Z:%u",
            (unsigned)bus.state,bus.shadow,bus.latched,radio.ready,
            HAL_GPIO_ReadPin(HAL1_OPEN_GPIO_Port,HAL1_OPEN_Pin)==GPIO_PIN_SET,
            HAL_GPIO_ReadPin(Flame_Detector_GPIO_Port,Flame_Detector_Pin)==GPIO_PIN_SET,
            HAL_GPIO_ReadPin(zc_GPIO_Port,zc_Pin)==GPIO_PIN_SET);
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
