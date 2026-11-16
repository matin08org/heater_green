#include "HmiDiag.h"
#include <assert.h>
#include <stdio.h>
extern const HmiFont monitor_font;
int main(int argc,char **argv)
{
    assert(argc==2);DiagLog_Init();HmiDiag diag;HmiDiag_Init(&diag);
    HmiDiag_Key(&diag,KEY_DEBUG);
    DiagLog_Write(100,DIAG_INFO,"BOOT","CPU: internal Flash; LCD: SPI DMA 16MHz");
    DiagLog_Write(110,DIAG_INFO,"KEY","OK held 5s -> DIAG; Cancel -> HOME");
    DiagLog_Write(120,DIAG_INFO,"ADC","TIM5 -> ADC1 scan -> circular DMA");
    DiagLog_Write(500,DIAG_DEBUG,"TC1","raw=5780 q4=2800 C=700.00 valid=1");
    DiagLog_Write(1000,DIAG_DEBUG,"TC2","raw=04E8 q4=157 C=39.25 valid=1");
    DiagLog_Write(2000,DIAG_WARN,"NRF","NO LINK; last value must not look fresh");
    DiagLog_Write(2100,DIAG_ERROR,"CO","UNCAL: ppm unavailable; raw ADC visible");
    DiagLog_Write(2200,DIAG_INFO,"MAP","One stepper / two opposed dampers; Q5 distribution fan");
    HmiDiagView view;assert(HmiDiag_Capture(&diag,&view,&monitor_font,480,320,"PREVIEW / EXAMPLE DATA - NOT BOARD")==HMI_OK);
    FILE *file=fopen(argv[1],"wb");assert(file);
    uint16_t pixels[480*2];
    for(unsigned y=0;y<320;y+=2) {
        HmiSurface s={pixels,480*2,480,2,480};
        assert(HmiDiag_Compose(&view,(HmiRect){0,(int)y,480,2},&s)==HMI_OK);
        assert(fwrite(pixels,sizeof(pixels),1,file)==1);
    }
    assert(!fclose(file));return 0;
}
