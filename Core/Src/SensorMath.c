#include "SensorMath.h"
#include <math.h>
#include <string.h>
static bool Ntc(uint16_t raw,const NtcCalibration *c,float *temperature)
{
    if(!c->calibrated||!raw||raw>=4095U||!isfinite(c->pullup_ohms)||
       !isfinite(c->r25_ohms)||!isfinite(c->beta)||c->pullup_ohms<=0||c->r25_ohms<=0||c->beta<=0)return false;
    float resistance=c->pullup_ohms*raw/(4095U-raw);
    float denominator=1.0f/298.15f+logf(resistance/c->r25_ohms)/c->beta;
    if(!isfinite(denominator)||denominator<=0)return false;
    *temperature=1.0f/denominator-273.15f;
    return isfinite(*temperature)&&*temperature>-273.15f&&*temperature<1000.0f;
}
void SensorMath_Convert(const uint16_t raw[6],const HmiDiagAnalog *e,
                        const BoardAnalogCalibration *c,BoardAnalogValues *out)
{
    if(!out)return;
    memset(out,0,sizeof(*out));
    if(!raw||!e||!c||!e->voltage_valid)return;
    float me2=0,mixer=0;
    out->me2_temperature_valid=Ntc(raw[2],&c->me2_ntc,&me2);
    out->mixer_temperature_valid=Ntc(raw[3],&c->mixer_ntc,&mixer);
    if(out->me2_temperature_valid)out->me2_temperature_q4=(int32_t)lroundf(me2*4.0f);
    if(out->mixer_temperature_valid)out->mixer_temperature_q4=(int32_t)lroundf(mixer*4.0f);
    if(!c->calibrated||!out->me2_temperature_valid||!isfinite(c->zero_uv)||
       !isfinite(c->sensitivity_uv_per_ppm)||c->sensitivity_uv_per_ppm==0||
       !isfinite(c->reference_c)||!isfinite(c->span_temp_coefficient))return;
    float factor=1.0f+c->span_temp_coefficient*(me2-c->reference_c);
    if(!isfinite(factor)||factor<=0)return;
    /* Software differential, preserving sub-millivolt resolution. Offset and
     * sensitivity (including polarity and TIA gain) come from board calibration. */
    float uv=(float)e->differential_raw*(float)e->vdda_mv*1000.0f/4095.0f;
    float ppm=(uv-c->zero_uv)/(c->sensitivity_uv_per_ppm*factor);
    if(!isfinite(ppm)||ppm<0||ppm>65535.0f)return;
    out->co_ppm=(uint16_t)lroundf(ppm);out->co_valid=true;
}
