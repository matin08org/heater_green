#include "HmiDiag.h"
#include "HmiDiagData.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
static uint32_t now;
static void Advance(KeyInput *keys,uint8_t pressed,unsigned ms)
{for(unsigned i=0;i<ms;i+=5) {now+=5;KeyInput_Update(keys,pressed,now);}}
static void Navigation(uint32_t start)
{
    KeyInput keys;HmiDiag diag;HmiDiag_Init(&diag);now=start;
    KeyInput_Init(&keys,now);Advance(&keys,0,80);KeyCode key;
    Advance(&keys,KEY_MASK(KEY_OK),5030);assert(!KeyInput_Read(&keys,&key));
    Advance(&keys,KEY_MASK(KEY_OK),10);assert(KeyInput_Read(&keys,&key)&&key==KEY_DEBUG);
    assert(HmiDiag_Key(&diag,key)&&diag.open&&diag.section==HMI_DIAG_SUMMARY);
    KeyInput_RequireRelease(&keys);Advance(&keys,KEY_MASK(KEY_OK),6000);assert(!KeyInput_Read(&keys,&key));
    Advance(&keys,0,80);assert(keys.armed&&!KeyInput_Read(&keys,&key));
    for(unsigned section=1;section<HMI_DIAG_SECTIONS;++section) {
        Advance(&keys,KEY_MASK(KEY_OK),50);Advance(&keys,0,50);
        assert(KeyInput_Read(&keys,&key)&&key==KEY_OK);
        assert(!HmiDiag_Key(&diag,key)&&diag.section==(HmiDiagSection)section);
    }
    Advance(&keys,KEY_MASK(KEY_CANCEL),50);Advance(&keys,0,50);
    assert(KeyInput_Read(&keys,&key)&&key==KEY_CANCEL);
    assert(HmiDiag_Key(&diag,key)&&!diag.open);
    assert(HmiDiag_Key(&diag,KEY_DEBUG)&&diag.section==HMI_DIAG_SUMMARY);
}
static void LogsAndRender(void)
{
    uint8_t pixels[95*7];memset(pixels,0xa8,sizeof(pixels));
    HmiFont font={pixels,sizeof(pixels),32,126,5,7,6,8,1,0};
    HmiDiag diag;HmiDiag_Init(&diag);DiagLog_Init();
    assert(HmiDiag_Key(&diag,KEY_DEBUG));
    assert(!HmiDiag_Key(&diag,KEY_OK)&&diag.section==HMI_DIAG_LOG);
    DiagLog_Write(100,DIAG_ERROR,"SPI2","Long error message that must wrap instead of hiding essential fields at the right side of the terminal");
    HmiDiagView v,copy;
    assert(HmiDiag_Capture(&diag,&v,&font,120,160,"SPI=ERROR")==HMI_OK);
    assert(strstr(v.header,"2/8")&&strstr(v.header,"EVENTS"));
    unsigned lines=0;for(unsigned i=0;i<v.line_count;++i)if(v.lines[i][0]) {++lines;assert(v.levels[i]==DIAG_ERROR);}
    assert(lines>1);
    copy=v;
    DiagLog_Write(200,DIAG_INFO,"TC2","valid 39.25 C");
    assert(!memcmp(&copy,&v,sizeof(v))); /* new logs cannot mutate active snapshot */
    (void)HmiDiag_Key(&diag,KEY_UP);assert(!diag.live);
    uint16_t old_scroll=diag.scroll;
    DiagLog_Write(300,DIAG_WARN,"CO","UNCAL");
    assert(HmiDiag_Capture(&diag,&v,&font,120,160,"LIVE")==HMI_OK);
    assert(diag.scroll>=old_scroll);
    uint16_t whole[120*160],tiled[120*160];
    HmiSurface surface={whole,120*160,120,160,120};
    assert(HmiDiag_Compose(&v,(HmiRect){0,0,120,160},&surface)==HMI_OK);
    for(unsigned y=0;y<160;y+=2) {
        HmiSurface tile={tiled+y*120,240,120,2,120};
        assert(HmiDiag_Compose(&v,(HmiRect){0,(int)y,120,2},&tile)==HMI_OK);
    }
    assert(!memcmp(whole,tiled,sizeof(whole)));
    assert(HmiDiag_Capture(&diag,&v,&font,10,10,NULL)==HMI_INVALID);
    for(unsigned i=0;i<200;++i)DiagLog_Write(i,DIAG_DEBUG,"TC","value=%u",i);
    assert(DiagLog_Overwritten()>0);
    (void)HmiDiag_Key(&diag,KEY_OK);assert(diag.section==HMI_DIAG_OUTPUTS);
    for(unsigned i=0;i<50;++i)(void)HmiDiag_Key(&diag,KEY_UP);
    assert(HmiDiag_Capture(&diag,&v,&font,120,160,NULL)==HMI_OK && diag.scroll>=50);
}
int main(void)
{
    uint8_t ring[128],dst[64];for(unsigned i=0;i<128;++i)ring[i]=(uint8_t)i;
    for(unsigned prev=0;prev<=128;++prev)for(unsigned pos=0;pos<=128;++pos) {
        size_t retained=0,n=HmiDiag_RxTail(dst,sizeof(dst),ring,sizeof(ring),prev,pos,&retained);
        size_t expected=pos>=prev?pos-prev:128-prev+pos;
        assert(n==expected && retained==(n>64?64:n));
        for(size_t i=0;i<retained;++i)assert(dst[i]==ring[(pos+128-retained+i)%128]);
    }
    uint16_t raw[]={2048,1024,4095,0,1500,1000};HmiDiagAnalog a;
    HmiDiag_Analog(raw,true,1500,1000,1500,&a);
    assert(a.voltage_valid && a.vdda_mv==3300 && a.millivolts[0]==1650 && a.millivolts[2]==3300);
    assert(a.differential_raw==1024 && a.differential_mv==825 && a.cpu_temperature_tenths==300);
    HmiDiag_Analog(raw,false,1500,1000,1500,&a);assert(!a.voltage_valid);
    HmiDiag_Analog(raw,true,0,1000,1500,&a);assert(!a.voltage_valid);
    raw[4]=0;HmiDiag_Analog(raw,true,1500,1000,1500,&a);assert(!a.voltage_valid);
    Navigation(0);Navigation(UINT32_MAX-1000U);LogsAndRender();
    puts("PASS terminal: OK-only 5s, one entry per hold, release guard, Cancel home, sections, wrapped colored logs, immutable snapshots, two-row rendering and tick wrap");
}
