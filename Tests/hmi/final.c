#include "HmiPages.h"
#include "HmiHome.h"
#include "SensorMath.h"
#include "RtuMaster.h"
#include "CombustionCycle.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern const HmiFont monitor_font;
static uint16_t full[480*320],tiled[480*320];
static void Pages(const char *output)
{
    HmiPageView v={.page=HmiPages_Find(hmi_pages,hmi_page_count,"home"),.font=&monitor_font,.focus=-1};
    assert(v.page);
    strcpy(v.values[HMI_BIND_TC1],"725.25 C");strcpy(v.values[HMI_BIND_TC2],"38.75 C");
    strcpy(v.values[HMI_BIND_CO],"UNCAL");strcpy(v.values[HMI_BIND_CO2],"812 ppm");
    strcpy(v.values[HMI_BIND_STATE],"INHIBITED");strcpy(v.values[HMI_BIND_MODE],"MANUAL");
    strcpy(v.values[HMI_BIND_TARGET],"Target 800 ppm");strcpy(v.values[HMI_BIND_SAMPLE],"IDLE");
    v.values[HMI_BIND_SOURCE][0]='\0'; /* Preview provenance stays in package docs. */
    HmiHomeState home={.inhibited=true,.co2_valid=true,.co2_ppm=812,.target_ppm=800};
    HmiHome_Apply(&v,&home);
    int run_index=-1,stop_index=-1;
    for(unsigned i=0;i<v.page->count;i++) {
        if(v.page->widgets[i].binding==HMI_BIND_RUN_STATE)run_index=(int)i;
        if(v.page->widgets[i].binding==HMI_BIND_STOP_STATE)stop_index=(int)i;
    }
    assert(run_index>=0&&stop_index>=0);v.focus=(int16_t)stop_index;
    HmiSurface s={full,480*320,480,320,480};
    assert(HmiPages_Compose(&v,(HmiRect){0,0,480,320},&s)==HMI_OK);
    for(unsigned y=0;y<320;y+=2) {
        HmiSurface tile={tiled+y*480,960,480,2,480};
        assert(HmiPages_Compose(&v,(HmiRect){0,(int)y,480,2},&tile)==HMI_OK);
    }
    assert(!memcmp(full,tiled,sizeof(full)));
    uint16_t crop[17*13];HmiSurface small={crop,17*13,17,13,17};
    assert(HmiPages_Compose(&v,(HmiRect){151,62,17,13},&small)==HMI_OK);
    for(unsigned y=0;y<13;++y)assert(!memcmp(crop+y*17,full+(y+62)*480+151,17*2));
    if(output) {FILE *f=fopen(output,"wb");assert(f);assert(fwrite(full,sizeof(full),1,f)==1);assert(!fclose(f));}
    if(output) {
        char path[1024];assert(strlen(output)+13U<sizeof(path));snprintf(path,sizeof(path),"%s.running",output);
        HmiPageView running=v;
        HmiHomeState demo={.configured=true,.running=true,.process_proven=true,.co_valid=true,.co_calibrated=true,
            .co_ppm=48,.co_limit_ppm=100,.co_accept_ppm=50,.co2_valid=true,.co2_ppm=620,.target_ppm=800};
        HmiHome_Apply(&running,&demo);running.values[HMI_BIND_SOURCE][0]='\0';
        running.focus=(int16_t)run_index;
        assert(HmiPages_Compose(&running,(HmiRect){0,0,480,320},&s)==HMI_OK);
        FILE *f=fopen(path,"wb");assert(f);assert(fwrite(full,sizeof(full),1,f)==1);assert(!fclose(f));
        /* Restore baseline for the crop-equivalence assertion above and later tests. */
        assert(HmiPages_Compose(&v,(HmiRect){0,0,480,320},&s)==HMI_OK);
    }
    assert(!HmiPages_Find(hmi_pages,hmi_page_count,"MISSING"));
    /* Palette-only updates must redraw exactly the selected panel and preserve
       every other panel, sensor value and dynamic badge. No new SPI reads. */
    HmiPageView palette_before=v;
    const uint16_t alarm_palette[]={0x3000,0xf800,0xffff,0xffe0};
    assert(HmiHome_SetPanelPalette("co2_panel",alarm_palette));
    HmiHome_Apply(&v,&home);
    unsigned images=0,changed_panels=0;HmiRect panel={0};
    for(unsigned i=0;i<v.page->count;++i) {
        const HmiWidget *w=&v.page->widgets[i];
        assert(w->binding!=HMI_BIND_FAULT_STATUS&&strcmp(w->id,"fault_panel")&&strcmp(w->id,"fault"));
        assert(w->kind!=HMI_PAGE_RGB565);
        if(w->kind==HMI_PAGE_IMAGE2||w->kind==HMI_PAGE_IMAGE2_AA)++images;
        if(HmiPages_WidgetChanged(&palette_before,&v,i)) {
            ++changed_panels;assert(!strcmp(w->id,"co2_panel"));panel=w->bounds;
        }
    }
    assert(images==17&&changed_panels==1);
    assert(!HmiPages_SetPalette(&v,"missing",alarm_palette));
    assert(!HmiHome_SetPanelPalette("missing",alarm_palette));
    assert(HmiPages_Compose(&v,(HmiRect){0,0,480,320},&s)==HMI_OK);
    unsigned changed_pixels=0;
    for(unsigned y=0;y<320;++y)for(unsigned x=0;x<480;++x)if(full[y*480+x]!=tiled[y*480+x]) {
        ++changed_pixels;assert(x>=(unsigned)panel.x&&x<(unsigned)panel.x+panel.w&&
            y>=(unsigned)panel.y&&y<(unsigned)panel.y+panel.h);
    }
    assert(changed_pixels>100);
    if(output) {
        char path[1024];snprintf(path,sizeof(path),"%s.palette",output);
        FILE *f=fopen(path,"wb");assert(f);assert(fwrite(full,sizeof(full),1,f)==1);assert(!fclose(f));
    }
    assert(HmiHome_SetPanelPalette("co2_panel",NULL));v=palette_before;
    puts("PASS 17 independent four-role panels without Fault Status: palette-only dirty update, isolated recolor and restore");
    HmiPageView run_selected=v;run_selected.focus=(int16_t)run_index;
    assert(!memcmp(run_selected.values,v.values,sizeof(v.values)));
    assert(!memcmp(run_selected.background,v.background,sizeof(v.background)));
    unsigned focus_changes=0;
    for(unsigned i=0;i<v.page->count;i++)if(HmiPages_WidgetChanged(&v,&run_selected,i)) {
        assert((int)i==run_index||(int)i==stop_index);++focus_changes;
    }
    assert(focus_changes==2);
    puts("PASS RUN/STOP focus: only old/new buttons dirty; selection never changes measured process state or status colors");
    HmiPageView old=v;home.automatic=true;HmiHome_Apply(&v,&home);
    assert(!strcmp(old.values[HMI_BIND_MODE_MANUAL],v.values[HMI_BIND_MODE_MANUAL]));
    assert(HmiPages_BindingChanged(&old,&v,HMI_BIND_MODE_MANUAL)); /* style-only repaint */
    assert(!HmiPages_BindingChanged(&old,&v,HMI_BIND_CO2_VALUE));
    assert(!strcmp(v.values[HMI_BIND_GREENHOUSE],"UNKNOWN"));
    home.process_proven=true;HmiHome_Apply(&v,&home);
    assert(!strcmp(v.values[HMI_BIND_GREENHOUSE],"OPEN")&&!strcmp(v.values[HMI_BIND_EXHAUST],"CLOSED"));
    home.bypass_proven=true;HmiHome_Apply(&v,&home);assert(!strcmp(v.values[HMI_BIND_GREENHOUSE],"FAULT"));
    assert(!strcmp(v.values[HMI_BIND_CO_VALUE],"---"));
    home.co_valid=true;home.co_ppm=20;home.co_limit_ppm=10;home.co_accept_ppm=5;
    HmiHome_Apply(&v,&home);assert(!strcmp(v.values[HMI_BIND_CO_STATUS],"HIGH"));
    assert(!strcmp(v.values[HMI_BIND_CO_VALUE],"20"));
    home.co_valid=false;home.co_calibrated=true;HmiHome_Apply(&v,&home);
    assert(!strcmp(v.values[HMI_BIND_CO_STATUS],"INVALID"));
    /* Explicit RGB565 LE resource, row padding and native centering. */
    const uint8_t rgb[]={0,0xf8,0xe0,7,0,0,0x1f,0,0xff,0xff};
    HmiRgbImage image={rgb,sizeof(rgb),2,2,6};
    HmiWidget widget={.kind=HMI_PAGE_RGB565,.bounds={2,3,2,2},.resource=&image};
    HmiPage p={"rgb",0,1,&widget};v.page=&p;
    uint16_t dest[6*6];HmiSurface d={dest,36,6,6,6};
    assert(HmiPages_Compose(&v,(HmiRect){0,0,6,6},&d)==HMI_OK);
    assert(dest[3*6+2]==0xf800&&dest[3*6+3]==0x07e0&&dest[4*6+2]==0x001f&&dest[4*6+3]==0xffff);
    puts("PASS pages: real PNG/live values, full vs two-row rendering, dirty crop, RGB565, dynamic colors, damper proofs and invalid-sample placeholders");
}
static void Analog(void)
{
    uint16_t raw[]={2048,1948,2048,2048,1500,1000};HmiDiagAnalog e;
    HmiDiag_Analog(raw,true,1500,1000,1500,&e);
    BoardAnalogCalibration c={.calibrated=true,.sensitivity_uv_per_ppm=100,.reference_c=25,
        .me2_ntc={true,4700,4700,3950},.mixer_ntc={true,4700,4700,3950}};
    BoardAnalogValues v;SensorMath_Convert(raw,&e,&c,&v);
    assert(v.co_valid&&v.co_ppm==806&&v.me2_temperature_valid&&v.me2_temperature_q4==100);
    c.calibrated=false;SensorMath_Convert(raw,&e,&c,&v);assert(!v.co_valid);
    c.calibrated=true;raw[2]=4095;SensorMath_Convert(raw,&e,&c,&v);assert(!v.co_valid&&!v.me2_temperature_valid);
    puts("PASS sensor math: software differential, calibrated ppm/NTC, missing calibration and rail fault rejection");
}
static bool tx_busy;static unsigned transmissions;
static bool Tx(void *unused,const uint8_t *data,uint16_t n) {(void)unused;assert(!tx_busy&&n>=8&&Rtu_Crc(data,n)==0);tx_busy=true;++transmissions;return true;}
static bool Busy(void *unused) {(void)unused;return tx_busy;}
static void Reply(uint8_t *data,unsigned size) {uint16_t c=Rtu_Crc(data,size-2);data[size-2]=(uint8_t)c;data[size-1]=(uint8_t)(c>>8);}
static void Serial(void)
{
    RtuMaster m;Rtu_Init(&m,(RtuIo){NULL,Tx,Busy},4,100,1);
    assert(Rtu_Read(&m,2,4,0,2,0));assert(!Rtu_Read(&m,1,3,0,4,0));
    Rtu_Process(&m,3);assert(!transmissions);Rtu_Process(&m,4);assert(transmissions==1);
    tx_busy=false;Rtu_Process(&m,5);
    uint8_t response[]={2,4,4,0,1,0x86,0xa0,0,0};Reply(response,sizeof(response));
    Rtu_Receive(&m,response,3,6);assert(m.rx_length==3);
    Rtu_Receive(&m,response+3,sizeof(response)-3,7);bool good;
    assert(Rtu_TakeResult(&m,&good)&&good&&m.completed==1);
    assert(Rtu_Read(&m,1,3,0,4,10));Rtu_Process(&m,11);assert(transmissions==2);
    tx_busy=false;Rtu_Process(&m,12);Rtu_Process(&m,112);assert(m.retry_count==1);
    Rtu_Process(&m,116);assert(transmissions==3);tx_busy=false;Rtu_Process(&m,117);Rtu_Process(&m,217);
    assert(Rtu_TakeResult(&m,&good)&&!good&&m.timeouts==2);
    assert(!Rtu_Read(&m,0,3,0,4,220));assert(!Rtu_Read(&m,2,4,0,29,220));
    uint16_t words[]={1,2};assert(Rtu_WriteRegisters(&m,1,4096,words,2,230));Rtu_Process(&m,230);
    tx_busy=false;Rtu_Process(&m,231);
    uint8_t ack[]={1,16,0x10,0,0,2,0,0};Reply(ack,sizeof(ack));Rtu_Receive(&m,ack,sizeof(ack),232);
    assert(Rtu_TakeResult(&m,&good)&&good);
    assert(Rtu_Read(&m,2,4,0,2,240));Rtu_Process(&m,240);tx_busy=false;Rtu_Process(&m,241);
    response[0]=1;Reply(response,sizeof(response));Rtu_Receive(&m,response,sizeof(response),242);
    assert(Rtu_TakeResult(&m,&good)&&!good&&m.protocol_errors);
    puts("PASS RTU master: one outstanding request, quiet time, fragmented valid reply, bounded timeout retry, FC16 acknowledgment and wrong node rejection");
}
static BurnerConfig Config(void)
{
    return (BurnerConfig){.commissioned=true,.adaptive_air_enabled=true,.cooling_mode=COOL_BY_TEMPERATURE,
        .purge_ms=30000,.required_purge_ms=30000,.interpurge_ms=30000,.postpurge_ms=20000,.ignition_ms=5000,
        .flame_stable_ms=200,.airflow_timeout_ms=5000,.input_max_age_ms=2000,.max_attempts=2,
        .co_min_wait_ms=1000,.co_timeout_ms=5000,.co_accept_ppm=5,.co_limit_ppm=10,.co_trip_ppm=50,.co2_hysteresis_ppm=100,
        .initial_measure_delay_ms=1,.sample_period_ms=300000,.sample_fill_ms=10000,.sample_transfer_ms=10000,
        .cooling_timeout_ms=60000,.air_settle_ms=30000,.damper_timeout_ms=3000,
        .air_initial_percent=40,.air_min_percent=20,.air_max_percent=80,.air_step_percent=10,.max_adjustments=3};
}
static void Samples(void)
{
    /* All numbers are arbitrary simulation fixtures, not an approved profile. */
    BurnerControl b;BurnerConfig c=Config();assert(Burner_ConfigValid(&c));Burner_Init(&b,&c,0);
    b.out.air_percent=40;
    BurnerInputs in={.flame_temp_valid=true,.flame_temp_q4=3200,.cooler_temp_valid=true,.cooler_temp_q4=170,.bypass_proven=true};
    assert(CombustionCycle_Process(&b,&in,0)==BURNER_NO_FAULT);
    assert(CombustionCycle_Process(&b,&in,1)==BURNER_NO_FAULT&&b.out.sample_valve_1);
    assert(CombustionCycle_Process(&b,&in,10001)==BURNER_NO_FAULT&&!b.out.sample_valve_1&&b.out.cooler_fan);
    in.cooler_temp_valid=false;assert(CombustionCycle_Process(&b,&in,10002)==BURNER_NO_FAULT&&b.out.cooler_fan);
    in.cooler_temp_valid=true;in.cooler_temp_q4=159;
    assert(CombustionCycle_Process(&b,&in,10003)==BURNER_NO_FAULT&&b.out.sample_valve_2);
    assert(CombustionCycle_Process(&b,&in,20003)==BURNER_NO_FAULT&&!b.out.sample_valve_2);
    in.co_valid=true;in.co_ppm=20;in.co_sample_ms=21003;
    assert(CombustionCycle_Process(&b,&in,21003)==BURNER_NO_FAULT&&b.sample_state==SAMPLE_AIR_SETTLE&&b.out.air_percent==50&&b.out.damper==DAMPER_BYPASS);
    assert(CombustionCycle_Process(&b,&in,51003)==BURNER_NO_FAULT&&b.sample_state==SAMPLE_WAIT_BYPASS);
    b.sample_state=SAMPLE_WAIT_CO;b.sample_ms=52000;in.co_sample_ms=53000;
    assert(CombustionCycle_Process(&b,&in,53000)==BURNER_ADJUST_LIMIT); /* no improvement */
    b.have_previous_adjust_co=false;b.sample_state=SAMPLE_WAIT_CO;b.sample_ms=54000;in.co_sample_ms=55000;in.co_ppm=4;
    assert(CombustionCycle_Process(&b,&in,55000)==BURNER_NO_FAULT&&b.out.damper==DAMPER_PROCESS);
    in.process_proven=true;
    assert(CombustionCycle_Process(&b,&in,55001)==BURNER_NO_FAULT&&b.out.distribution_fan);
    assert(CombustionCycle_Process(&b,&in,355000)==BURNER_NO_FAULT&&b.out.damper==DAMPER_BYPASS&&!b.out.distribution_fan);
    b.sample_state=SAMPLE_WAIT_CO;b.sample_ms=400000;in.co_sample_ms=401000;in.co_ppm=50;
    assert(CombustionCycle_Process(&b,&in,401000)==BURNER_CO_HIGH);
    b.sample_state=SAMPLE_WAIT_CO;b.sample_ms=410000;in.co_sample_ms=411000;in.co_ppm=4;in.flame_temp_q4=2700;
    assert(CombustionCycle_Process(&b,&in,411000)==BURNER_NO_FAULT&&b.out.air_percent==40);
    b.sample_state=SAMPLE_COOLING;b.sample_ms=420000;in.cooler_temp_valid=false;
    assert(CombustionCycle_Process(&b,&in,480000)==BURNER_COOLING_TIMEOUT);
    puts("PASS revised cycle: trapped sample, measured <40C, bounded air trim/recheck, no-improvement trip, high-CO trip, low-temp trim, periodic bypass and cooling timeout");
}
int main(int argc,char **argv) {Pages(argc>1?argv[1]:NULL);Analog();Serial();Samples();return 0;}
