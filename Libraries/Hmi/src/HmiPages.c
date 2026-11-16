#include "HmiPages.h"
#include <string.h>
_Static_assert(HMI_BIND_COUNT<=32,"style_mask must cover every binding");
bool HmiPages_SetPalette(HmiPageView *v,const char *id,const uint16_t colors[4])
{
    if(!v||!v->page||!id||!colors||v->page->count>128U)return false;
    for(unsigned n=0;n<v->page->count;++n) {
        const HmiWidget *w=&v->page->widgets[n];
        if((w->kind==HMI_PAGE_IMAGE2||w->kind==HMI_PAGE_IMAGE2_AA)&&w->id&&!strcmp(w->id,id)) {
            memcpy(v->palettes[n],colors,sizeof(v->palettes[n]));
            v->palette_mask[n/32U]|=1UL<<(n%32U);return true;
        }
    }
    return false;
}
static const uint16_t *Palette(const HmiPageView *v,unsigned n)
{
    const HmiWidget *w=&v->page->widgets[n];
    return v->palette_mask[n/32U]&(1UL<<(n%32U))?v->palettes[n]:
        v->focus==(int16_t)n?w->selected_colors:w->colors;
}
bool HmiPages_WidgetChanged(const HmiPageView *a,const HmiPageView *b,unsigned n)
{
    if(!a||!b||a->page!=b->page||!b->page||n>=b->page->count||n>=128U)return true;
    const HmiWidget *w=&b->page->widgets[n];
    if((a->focus==(int16_t)n)!=(b->focus==(int16_t)n))return true;
    if(w->kind==HMI_PAGE_IMAGE2||w->kind==HMI_PAGE_IMAGE2_AA)return memcmp(Palette(a,n),Palette(b,n),8)!=0;
    return w->binding!=HMI_BIND_NONE&&HmiPages_BindingChanged(a,b,w->binding);
}
bool HmiPages_BindingChanged(const HmiPageView *a,const HmiPageView *b,HmiBinding binding)
{
    if(!a||!b||(unsigned)binding>=HMI_BIND_COUNT)return true;
    uint32_t mask=1UL<<binding;
    return strcmp(a->values[binding],b->values[binding]) || ((a->style_mask^b->style_mask)&mask) ||
        ((b->style_mask&mask)&&(a->foreground[binding]!=b->foreground[binding]||a->background[binding]!=b->background[binding]));
}
static HmiResult Badge(HmiSurface *s,HmiRect r,uint8_t radius,uint16_t color)
{
    unsigned rad=radius;
    if(rad>r.w/2U)rad=r.w/2U;
    if(rad>r.h/2U)rad=r.h/2U;
    for(unsigned y=0;y<r.h;++y) {
        int64_t dy=(int64_t)r.y+y;if(dy<0||dy>=s->height)continue;
        unsigned shine=r.h>1?3U*(r.h-1U-y)/(r.h-1U):0;
        unsigned rr=(((color>>11)&31U)*(32U-shine)+31U*shine)/32U;
        unsigned gg=(((color>>5)&63U)*(32U-shine)+63U*shine)/32U;
        unsigned bb=((color&31U)*(32U-shine)+31U*shine)/32U;
        uint16_t paint=(uint16_t)((rr<<11)|(gg<<5)|bb);
        for(unsigned x=0;x<r.w;x++) {
            int64_t dx=(int64_t)r.x+x;if(dx<0||dx>=s->width)continue;
            unsigned coverage=16;
            if(rad&&(x<rad||x>=r.w-rad)&&(y<rad||y>=r.h-rad)) {
                coverage=0;int cx=(x<rad?rad:r.w-rad)*8,cy=(y<rad?rad:r.h-rad)*8;
                for(unsigned sy=0;sy<4;sy++)for(unsigned sx=0;sx<4;sx++) {
                    int xx=(int)(8*x+2*sx+1)-cx,yy=(int)(8*y+2*sy+1)-cy;
                    if((unsigned)(xx*xx+yy*yy)<=rad*rad*64U)++coverage;
                }
            }
            uint16_t *dst=&s->pixels[(size_t)dy*s->stride+dx];
            if(coverage==16)*dst=paint;
            else if(coverage) {
                uint16_t bg=*dst;unsigned inv=16U-coverage;
                unsigned br=(rr*coverage+((bg>>11)&31U)*inv+8U)/16U;
                unsigned bgc=(gg*coverage+((bg>>5)&63U)*inv+8U)/16U;
                unsigned bc=(bb*coverage+(bg&31U)*inv+8U)/16U;
                *dst=(uint16_t)((br<<11)|(bgc<<5)|bc);
            }
        }
    }
    return HMI_OK;
}
const HmiPage *HmiPages_Find(const HmiPage *pages,uint16_t count,const char *id)
{
    if(!pages||!id)return NULL;
    for(unsigned i=0;i<count;++i)if(pages[i].id&&!strcmp(pages[i].id,id))return &pages[i];
    return NULL;
}
static HmiResult Rgb(HmiSurface *s,const HmiRgbImage *i,HmiRect r,HmiRect tile)
{
    if(!i||!i->data||!i->width||!i->height||i->width>32767U||
       i->stride<(uint32_t)i->width*2U||
       (size_t)(i->height-1U)*i->stride+(size_t)i->width*2U>i->size||
       r.w!=i->width||r.h!=i->height)return HMI_INVALID;
    for(unsigned y=0;y<tile.h;++y) {
        int64_t sy=(int64_t)tile.y+y-r.y;if(sy<0||sy>=r.h)continue;
        uint16_t *dst=s->pixels+(size_t)y*s->stride;
        for(unsigned x=0;x<tile.w;++x) {
            int64_t sx=(int64_t)tile.x+x-r.x;if(sx<0||sx>=r.w)continue;
            const uint8_t *p=i->data+(size_t)sy*i->stride+(size_t)sx*2U;
            dst[x]=(uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8));
        }
    }
    return HMI_OK;
}
HmiResult HmiPages_DrawWidget(const HmiPageView *v,unsigned n,const HmiWidget *w,HmiRect tile,HmiSurface *s)
{
    HmiResult result=HMI_OK;
    int32_t x=w->bounds.x-tile.x,y=w->bounds.y-tile.y;
    if(w->kind==HMI_PAGE_IMAGE2_AA) {
        const HmiImage2Aa *image=w->resource;
        if(!HmiImage2Aa_Valid(image)||image->width!=w->bounds.w||image->height!=w->bounds.h)return HMI_INVALID;
        return HmiImage2Aa_Draw(s,image,x,y,Palette(v,n));
    }
    if(w->kind==HMI_PAGE_IMAGE2) {
        const HmiImage2 *image=w->resource;
        if(!HmiImage2_Valid(image)||image->width!=w->bounds.w||image->height!=w->bounds.h)return HMI_INVALID;
        HmiPalette2 palette;HmiPalette2_Set(&palette,Palette(v,n));
        return HmiImage2_Draw(s,image,(HmiRect){0,0,image->width,image->height},x,y,&palette);
    }
    if(w->kind==HMI_PAGE_RGB565)return Rgb(s,w->resource,w->bounds,tile);
    if(w->kind!=HMI_PAGE_TEXT&&w->kind!=HMI_PAGE_BADGE)return HMI_INVALID;
    if((unsigned)w->binding>=HMI_BIND_COUNT)return HMI_INVALID;
    const char *text=w->binding==HMI_BIND_NONE?w->resource:v->values[w->binding];
    HmiTextStyle style={w->foreground,w->background,w->align,w->opaque};
    if(v->style_mask&(1UL<<w->binding)) {style.foreground=v->foreground[w->binding];style.background=v->background[w->binding];}
    const HmiFont *font=w->font?w->font:v->font;
    if(!HmiFont_Valid(font))return HMI_INVALID;
    bool selected=v->focus==(int16_t)n;
    if(selected&&w->kind!=HMI_PAGE_BADGE) {style.background=w->selected_colors[0];style.opaque=true;}
    HmiRect text_rect={x,y,w->bounds.w,w->bounds.h};
    if(w->kind==HMI_PAGE_BADGE) {
        if(selected&&text_rect.w>4U&&text_rect.h>4U) {
            result=Badge(s,text_rect,w->radius,0xffe0);if(result!=HMI_OK)return result;
            HmiRect inner={text_rect.x+2,text_rect.y+2,text_rect.w-4U,text_rect.h-4U};
            result=Badge(s,inner,w->radius>2?w->radius-2U:0,style.background);if(result!=HMI_OK)return result;
        } else {
        result=Badge(s,text_rect,w->radius,style.background);if(result!=HMI_OK)return result;
        }
        if(w->bounds.h>font->height)text_rect.y+=(w->bounds.h-font->height)/2U;
        style.opaque=false;
    }
    return HmiText_Draw(s,text_rect,font,text,48,style);
}
HmiResult HmiPages_Compose(void *snapshot,HmiRect tile,HmiSurface *s)
{
    HmiPageView *v=snapshot;
    if(!v||!v->page||!HmiFont_Valid(v->font)||!HmiSurface_Valid(s)||
       s->width!=tile.w||s->height!=tile.h||tile.x<0||tile.y<0||
       (int64_t)tile.x+tile.w>480||(int64_t)tile.y+tile.h>320||
       v->page->count>128U||(v->page->count&&!v->page->widgets))return HMI_INVALID;
    HmiResult result=HmiSurface_Fill(s,(HmiRect){0,0,tile.w,tile.h},v->page->background);
    if(result!=HMI_OK)return result;
    for(unsigned n=0;n<v->page->count;++n) {
        const HmiWidget *w=&v->page->widgets[n];
        if((int64_t)w->bounds.x+w->bounds.w<=tile.x||w->bounds.x>=(int64_t)tile.x+tile.w||
           (int64_t)w->bounds.y+w->bounds.h<=tile.y||w->bounds.y>=(int64_t)tile.y+tile.h)continue;
        result=HmiPages_DrawWidget(v,n,w,tile,s);
        if(result!=HMI_OK)return result;
    }
    return HMI_OK;
}
