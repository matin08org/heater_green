#include "HmiHome.h"
#include <stdio.h>
#include <string.h>
static const char *const panel_ids[]={"header","mode_panel","manual_frame","auto_frame",
    "state_panel","run_frame","stop_frame","temperature_panel",
    "co_panel","co_value_frame","co_quality_frame","damper_panel","greenhouse_frame",
    "exhaust_frame","target_panel","co2_panel","footer_panel"};
static uint32_t panel_mask;
static uint16_t panel_colors[sizeof(panel_ids)/sizeof(panel_ids[0])][4];
bool HmiHome_SetPanelPalette(const char *id,const uint16_t colors[4])
{
    if(!id)return false;
    for(unsigned i=0;i<sizeof(panel_ids)/sizeof(panel_ids[0]);++i)if(!strcmp(id,panel_ids[i])) {
        if(colors){memcpy(panel_colors[i],colors,8);panel_mask|=1UL<<i;}
        else panel_mask&=~(1UL<<i);
        return true;
    }
    return false;
}
static void Text(HmiPageView *v,HmiBinding binding,const char *text)
{snprintf(v->values[binding],sizeof(v->values[binding]),"%s",text);}
static void Style(HmiPageView *v,HmiBinding binding,uint16_t fg,uint16_t bg)
{v->style_mask|=1UL<<binding;v->foreground[binding]=fg;v->background[binding]=bg;}
void HmiHome_Apply(HmiPageView *v,const HmiHomeState *s)
{
    if(!v||!s)return;
    for(unsigned i=0;i<sizeof(panel_ids)/sizeof(panel_ids[0]);++i)
        if(panel_mask&(1UL<<i))(void)HmiPages_SetPalette(v,panel_ids[i],panel_colors[i]);
    uint16_t white=0xffff,muted=Hmi_RGB565(141,170,194),dark=Hmi_RGB565(16,40,62);
    uint16_t blue=Hmi_RGB565(0,174,231),green=Hmi_RGB565(35,185,65),red=Hmi_RGB565(184,38,48);
    uint16_t amber=Hmi_RGB565(182,105,10),lime=Hmi_RGB565(110,241,109);
    Text(v,HMI_BIND_MODE_MANUAL,"MANUAL");Text(v,HMI_BIND_MODE_AUTO,"AUTO");
    Style(v,HMI_BIND_MODE_MANUAL,s->automatic?muted:white,s->automatic?dark:blue);
    Style(v,HMI_BIND_MODE_AUTO,s->automatic?white:muted,s->automatic?green:dark);
    Text(v,HMI_BIND_RUN_STATE,s->starting?"STARTING":"RUN");
    Style(v,HMI_BIND_RUN_STATE,s->inhibited||s->starting||s->running?white:muted,
          s->inhibited||s->starting?amber:s->running?green:dark);
    Text(v,HMI_BIND_STOP_STATE,s->stopping?"SHUTDOWN":"STOP");
    Style(v,HMI_BIND_STOP_STATE,s->stopping||s->stopped_proven?white:muted,
          s->stopping?amber:s->stopped_proven?red:dark);
    Text(v,HMI_BIND_FAULT_STATUS,s->faulted?"FAULT":s->configured?"NONE":"CONFIG");
    Style(v,HMI_BIND_FAULT_STATUS,s->faulted||!s->configured?white:muted,s->faulted?red:!s->configured?amber:dark);
    bool conflict=s->process_proven&&s->bypass_proven;
    const char *gh=conflict?"FAULT":s->process_proven?"OPEN":s->bypass_proven?"CLOSED":"UNKNOWN";
    const char *ex=conflict?"FAULT":s->bypass_proven?"OPEN":s->process_proven?"CLOSED":"UNKNOWN";
    Text(v,HMI_BIND_GREENHOUSE,gh);Text(v,HMI_BIND_EXHAUST,ex);
    Style(v,HMI_BIND_GREENHOUSE,conflict||s->process_proven?white:muted,conflict?red:s->process_proven?green:dark);
    Style(v,HMI_BIND_EXHAUST,conflict||s->bypass_proven?white:muted,conflict?red:s->bypass_proven?blue:dark);
    bool high=s->co_valid&&s->co_limit_ppm&&s->co_ppm>s->co_limit_ppm;
    bool good=s->co_valid&&s->co_limit_ppm&&s->co_ppm<=s->co_accept_ppm;
    Text(v,HMI_BIND_CO_STATUS,!s->co_valid?(s->co_calibrated?"INVALID":"UNCAL"):!s->co_limit_ppm?"NO LIMIT":high?"HIGH":good?"OK":"CHECK");
    Style(v,HMI_BIND_CO_STATUS,white,high?red:good?green:amber);
    if(s->co_valid)(void)HmiText_U32(v->values[HMI_BIND_CO_VALUE],48,s->co_ppm);else Text(v,HMI_BIND_CO_VALUE,"---");
    Style(v,HMI_BIND_CO_VALUE,high?Hmi_RGB565(255,111,111):good?white:Hmi_RGB565(255,206,112),dark);
    if(s->co2_valid)(void)HmiText_U32(v->values[HMI_BIND_CO2_VALUE],48,s->co2_ppm);else Text(v,HMI_BIND_CO2_VALUE,"---");
    Style(v,HMI_BIND_CO2_VALUE,s->co2_valid?lime:muted,dark);
    Text(v,HMI_BIND_CO2_STATUS,s->co2_valid?"LIVE":s->co2_link_stale?"NO LINK":"INVALID");
    Style(v,HMI_BIND_CO2_STATUS,s->co2_valid?lime:Hmi_RGB565(255,206,112),dark);
    (void)HmiText_U32(v->values[HMI_BIND_TARGET_VALUE],48,s->target_ppm);
    Style(v,HMI_BIND_TARGET_VALUE,white,dark);
}
