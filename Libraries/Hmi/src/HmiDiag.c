#include "HmiDiag.h"
#include <stdio.h>
#include <string.h>
void HmiDiag_Init(HmiDiag *d)
{
    if (!d) return;
    memset(d,0,sizeof(*d)); d->live=true;
}
bool HmiDiag_Key(HmiDiag *d, KeyCode key)
{
    if (!d) return false;
    if (key==KEY_DEBUG && !d->open) {
        d->open=true;d->live=true;d->scroll=0;d->section=HMI_DIAG_SUMMARY;d->seen_sequence=DiagLog_Sequence();
        ++d->revision;return true;
    }
    if (!d->open) return false;
    if (key==KEY_CANCEL) {d->open=false;++d->revision;return true;}
    if (key==KEY_OK) {d->section=(HmiDiagSection)((d->section+1U)%HMI_DIAG_SECTIONS);
        d->live=true;d->scroll=0;d->seen_sequence=DiagLog_Sequence();++d->revision;}
    if (key==KEY_UP || key==KEY_DOWN) {
        if(d->live && d->section==HMI_DIAG_LOG) {d->live=false;d->scroll=0;}
        if(key==KEY_UP && d->scroll<DIAG_LOG_CAPACITY-1U) ++d->scroll;
        if(key==KEY_DOWN && d->scroll) --d->scroll;
        if(key==KEY_DOWN && !d->scroll && d->section==HMI_DIAG_LOG)d->live=true;
        ++d->revision;
    }
    return false;
}
static size_t Format(const DiagEntry *e, char *out, size_t capacity)
{
    static const char levels[]="DIWE";
    int n=snprintf(out,capacity,"%lu.%03lu %c %s %s",
        (unsigned long)(e->ms/1000U),(unsigned long)(e->ms%1000U),
        (unsigned)e->level<=DIAG_ERROR?levels[e->level]:'?',e->tag,e->text);
    return n<0?0U:(size_t)n>=capacity?capacity-1U:(size_t)n;
}
HmiResult HmiDiag_Capture(HmiDiag *d,HmiDiagView *v,const HmiFont *font,
                         uint16_t width,uint16_t height,const char *status)
{
    if(!d || !v || !HmiFont_Valid(font) || width<40U ||
       height<4U*font->line_height+16U) return HMI_INVALID;
    unsigned columns=(width-16U)/font->advance;
    if(columns<8U) return HMI_INVALID;
    if(columns>=HMI_DIAG_COLUMNS) columns=HMI_DIAG_COLUMNS-1U;
    unsigned rows=(height-16U)/font->line_height-3U;
    if(rows>HMI_DIAG_LINES) rows=HMI_DIAG_LINES;
    uint16_t count=DiagLog_Count();
    uint32_t sequence=DiagLog_Sequence();
    if(d->section==HMI_DIAG_LOG && !d->live) {
        uint32_t added=sequence-d->seen_sequence;
        uint32_t offset=(uint32_t)d->scroll+added;
        d->scroll=(uint16_t)(offset>=DIAG_LOG_CAPACITY?DIAG_LOG_CAPACITY-1U:offset);
    } else if(d->section==HMI_DIAG_LOG) d->scroll=0;
    d->seen_sequence=sequence;
    if(d->section==HMI_DIAG_LOG) {
        if(!count)d->scroll=0;
        else if(d->scroll>=count)d->scroll=count-1U;
    }
    memset(v,0,sizeof(*v));v->font=font;v->width=width;v->height=height;
    v->columns=(uint8_t)columns;v->line_count=(uint8_t)rows;v->background=0x0841U;
    static const char *names[]={"SUMMARY","EVENTS","OUTPUTS","SENSORS","COMMS","ADV GPIO","ADV ADC","ADV SYSTEM"};
    snprintf(v->header,sizeof(v->header),"DIAG %u/%u | %s | %s",(unsigned)d->section+1U,HMI_DIAG_SECTIONS,names[d->section],d->live?"LIVE":"PAUSED");
    snprintf(v->status,sizeof(v->status),"%.*s",(int)columns,status?status:"");
    snprintf(v->footer,sizeof(v->footer),"OK next | UP/DN scroll | CANCEL home");
    if(d->section!=HMI_DIAG_LOG)return HMI_OK;
    /* Work backwards, retaining the newest rows that fit. Wrap complete log
     * messages instead of silently losing the right half of error details. */
    int end=(int)count-(int)d->scroll, first=end;
    unsigned used=0;
    char formatted[128];DiagEntry entry;
    while(first>0) {
        if(!DiagLog_Read((uint16_t)(first-1),&entry)) break;
        size_t n=Format(&entry,formatted,sizeof(formatted));
        unsigned needed=(unsigned)((n+columns-1U)/columns);
        if(!needed)needed=1;
        if(used+needed>rows && used)break;
        --first;used+=needed;
        if(used>=rows)break;
    }
    unsigned line=0;
    for(int index=first;index<end && line<rows;++index) {
        if(!DiagLog_Read((uint16_t)index,&entry))break;
        size_t n=Format(&entry,formatted,sizeof(formatted));
        for(size_t offset=0;offset<n && line<rows;offset+=columns) {
            size_t chunk=n-offset;if(chunk>columns)chunk=columns;
            memcpy(v->lines[line],formatted+offset,chunk);
            v->levels[line]=(unsigned)entry.level<=DIAG_ERROR?entry.level:DIAG_ERROR;
            ++line;
        }
    }
    return HMI_OK;
}
static HmiResult Line(HmiSurface *s,const HmiDiagView *v,HmiRect tile,
                      int32_t y,const char *text,uint16_t color)
{
    if((int64_t)y+v->font->height<=tile.y || y>=(int64_t)tile.y+tile.h)return HMI_OK;
    HmiTextStyle style={color,0,HMI_TEXT_LEFT,false};
    return HmiText_Draw(s,(HmiRect){8-tile.x,y-tile.y,(uint16_t)(v->width-16U),v->font->height},
                         v->font,text,HMI_DIAG_COLUMNS,style);
}
HmiResult HmiDiag_Compose(void *snapshot,HmiRect tile,HmiSurface *s)
{
    const HmiDiagView *v=snapshot;
    if(!v || !HmiFont_Valid(v->font) || !s || tile.x<0 || tile.y<0 ||
       tile.w!=s->width || tile.h!=s->height ||
       (int64_t)tile.x+tile.w>v->width || (int64_t)tile.y+tile.h>v->height ||
       v->line_count>HMI_DIAG_LINES) return HMI_INVALID;
    HmiResult r=HmiSurface_Fill(s,(HmiRect){0,0,tile.w,tile.h},v->background);
    if(r!=HMI_OK)return r;
    r=Line(s,v,tile,8,v->header,0xffff);if(r!=HMI_OK)return r;
    r=Line(s,v,tile,8+v->font->line_height,v->status,0x8df7);if(r!=HMI_OK)return r;
    static const uint16_t colors[]={0xad55,0x6ef3,0xfe88,0xfb8e};
    for(unsigned i=0;i<v->line_count;++i) {
        unsigned level=(unsigned)v->levels[i];if(level>DIAG_ERROR)level=DIAG_ERROR;
        r=Line(s,v,tile,8+(int32_t)(i+2U)*v->font->line_height,v->lines[i],colors[level]);
        if(r!=HMI_OK)return r;
    }
    return Line(s,v,tile,v->height-8-v->font->height,v->footer,0xffff);
}
