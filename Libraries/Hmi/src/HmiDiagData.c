#include "HmiDiagData.h"
#include <string.h>
void HmiDiag_Analog(const uint16_t raw[6],bool fresh,uint16_t vref_cal,
                    uint16_t temp_cal_30,uint16_t temp_cal_110,HmiDiagAnalog *out)
{
    if(!out)return;
    memset(out,0,sizeof(*out));if(!raw)return;
    out->differential_raw=(int32_t)raw[0]-raw[1];
    if(!fresh || !vref_cal || vref_cal>4095U || !raw[4])return;
    for(unsigned i=0;i<6;++i)if(raw[i]>4095U)return;
    out->vdda_mv=3300U*vref_cal/raw[4];
    if(out->vdda_mv<1800U || out->vdda_mv>3600U)return;
    out->voltage_valid=true;
    for(unsigned i=0;i<6;++i)out->millivolts[i]=(uint16_t)((uint32_t)raw[i]*out->vdda_mv/4095U);
    out->differential_mv=out->differential_raw*(int32_t)out->vdda_mv/4095;
    if(temp_cal_30>0U && temp_cal_110>temp_cal_30 && temp_cal_110<=4095U) {
        out->cpu_temperature_tenths=300+
            ((int32_t)((uint32_t)raw[5]*out->vdda_mv/3300U)-temp_cal_30)*800/(temp_cal_110-temp_cal_30);
        out->cpu_temperature_valid=true;
    }
}
size_t HmiDiag_RxTail(uint8_t *dst,size_t capacity,const uint8_t *ring,
                       size_t size,size_t previous,size_t position,size_t *retained)
{
    if(retained)*retained=0;
    if(!dst || !capacity || !ring || !size || !retained || previous>size || position>size)return 0;
    size_t length=position>=previous?position-previous:size-previous+position;
    size_t keep=length>capacity?capacity:length;
    /* Avoid arithmetic overflow, including unusually large host-test sizes. */
    size_t end=position==size?0:position;
    size_t index=end>=keep?end-keep:size-(keep-end);
    for(size_t i=0;i<keep;++i) {dst[i]=ring[index];if(++index==size)index=0;}
    *retained=keep;return length;
}
