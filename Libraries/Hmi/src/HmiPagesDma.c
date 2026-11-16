#include "HmiPagesDma.h"
#include <string.h>
enum {IDLE,WAIT_IMAGE,WAIT_FONT};
HmiResult HmiPagesDma_Init(HmiPagesDma *d,HmiAssetIo io,const HmiPageView *view)
{
    if(!d||!io.start||!io.poll||!view)return HMI_INVALID;
    memset(d,0,sizeof(*d));d->io=io;d->view=view;return HMI_OK;
}
static HmiResult Failure(HmiPagesDma *d,HmiResult r)
{d->error=r==HMI_INVALID?HMI_INVALID:HMI_IO_ERROR;return d->error;}
const HmiFont *HmiPagesDma_CachedFont(const HmiPagesDma *d,const HmiFont *source)
{
    if(!d||!source)return NULL;
    for(unsigned i=0;i<d->font_count;++i)if(d->fonts[i].source==source&&d->fonts[i].ready)return &d->fonts[i].ram;
    return NULL;
}
static HmiResult Read(HmiPagesDma *d,const uint8_t *src,uint8_t *dst,size_t bytes,uint8_t phase)
{
    HmiResult r=d->io.start(d->io.context,src,dst,bytes);
    if(r==HMI_BUSY)return HMI_BUSY;
    if(r!=HMI_OK)return Failure(d,r);
    d->phase=phase;++d->reads;d->bytes+=(uint32_t)bytes;return HMI_BUSY;
}
HmiResult HmiPagesDma_Compose(void *context,HmiRect tile,HmiSurface *s)
{
    HmiPagesDma *d=context;
    if(!d||!d->view||!d->view->page||!HmiSurface_Valid(s)||
       s->width!=tile.w||s->height!=tile.h||tile.x<0||tile.y<0||
       (int64_t)tile.x+tile.w>480||(int64_t)tile.y+tile.h>320||d->view->page->count>128U)return HMI_INVALID;
    if(d->error)return d->error;
    if(!d->busy) {
        d->busy=true;d->tile=tile;d->pixels=s->pixels;d->index=d->row=d->end_row=0;
        HmiResult r=HmiSurface_Fill(s,(HmiRect){0,0,tile.w,tile.h},d->view->page->background);
        if(r!=HMI_OK)return Failure(d,r);
    } else if(d->pixels!=s->pixels||d->tile.x!=tile.x||d->tile.y!=tile.y||d->tile.w!=tile.w||d->tile.h!=tile.h)
        return Failure(d,HMI_INVALID);
    if(d->phase!=IDLE) {
        HmiResult r=d->io.poll(d->io.context);
        if(r==HMI_BUSY)return r;
        if(r!=HMI_OK)return Failure(d,r);
        if(d->phase==WAIT_FONT)d->fonts[d->font_slot].ready=true;
        else {
            r=HmiPages_DrawWidget(d->view,d->index,&d->staged,tile,s);
            if(r!=HMI_OK)return Failure(d,r);
            d->row=(uint16_t)(d->row+d->staged.bounds.h);
            if(d->row>=d->end_row){++d->index;d->row=d->end_row=0;}
            d->phase=IDLE;
        }
        d->phase=IDLE;
    }
    const HmiPage *p=d->view->page;
    while(d->index<p->count) {
        const HmiWidget *w=&p->widgets[d->index];HmiRect b=w->bounds;
        if((int64_t)b.x+b.w<=tile.x||b.x>=(int64_t)tile.x+tile.w||
           (int64_t)b.y+b.h<=tile.y||b.y>=(int64_t)tile.y+tile.h){++d->index;continue;}
        d->staged=*w;
        if(w->kind==HMI_PAGE_IMAGE2||w->kind==HMI_PAGE_RGB565||w->kind==HMI_PAGE_IMAGE2_AA) {
            size_t size;uint16_t stride,width,height;const uint8_t *data;
            if(w->kind==HMI_PAGE_IMAGE2) {
                const HmiImage2 *im=w->resource;if(!HmiImage2_Valid(im))return Failure(d,HMI_INVALID);
                data=im->data;size=im->size;stride=im->stride;width=im->width;height=im->height;
            } else if(w->kind==HMI_PAGE_IMAGE2_AA) {
                const HmiImage2Aa *im=w->resource;if(!HmiImage2Aa_Valid(im))return Failure(d,HMI_INVALID);
                data=im->data;size=im->size;stride=im->stride;width=im->width;height=im->height;
            } else {
                const HmiRgbImage *im=w->resource;if(!im||!im->data)return Failure(d,HMI_INVALID);
                data=im->data;size=im->size;stride=im->stride;width=im->width;height=im->height;
                if(stride<(uint32_t)width*2U)return Failure(d,HMI_INVALID);
            }
            if(width!=b.w||height!=b.h||!stride||stride>sizeof(d->scratch))return Failure(d,HMI_INVALID);
            if(!d->end_row) {
                d->row=(uint16_t)(tile.y>b.y?tile.y-b.y:0);
                int64_t end=(int64_t)tile.y+tile.h-b.y;
                d->end_row=(uint16_t)(end<height?end:height);
            }
            unsigned rows=(unsigned)(sizeof(d->scratch)/stride);
            if(rows>(unsigned)(d->end_row-d->row))rows=(unsigned)(d->end_row-d->row);
            size_t offset=(size_t)d->row*stride,bytes=(size_t)rows*stride;
            /* Last source row may have no padding: fetch only valid tail bytes. */
            size_t used_row=w->kind==HMI_PAGE_IMAGE2?((size_t)width+3U)/4U:w->kind==HMI_PAGE_IMAGE2_AA?width:(size_t)width*2U;
            if(rows==0||offset>size)return Failure(d,HMI_INVALID);
            if(bytes>size-offset)bytes=(size_t)(rows-1U)*stride+used_row;
            if(bytes>size-offset)return Failure(d,HMI_INVALID);
            d->staged.bounds.y=b.y+d->row;d->staged.bounds.h=(uint16_t)rows;
            if(w->kind==HMI_PAGE_IMAGE2) {
                d->image=(HmiImage2){d->scratch,bytes,width,(uint16_t)rows,stride};d->staged.resource=&d->image;
            } else if(w->kind==HMI_PAGE_IMAGE2_AA) {
                d->aa=(HmiImage2Aa){d->scratch,bytes,width,(uint16_t)rows,stride};d->staged.resource=&d->aa;
            } else {
                d->rgb=(HmiRgbImage){d->scratch,bytes,width,(uint16_t)rows,stride};d->staged.resource=&d->rgb;
            }
            return Read(d,data+offset,d->scratch,bytes,WAIT_IMAGE);
        }
        if(w->kind!=HMI_PAGE_TEXT&&w->kind!=HMI_PAGE_BADGE)return Failure(d,HMI_INVALID);
        const HmiFont *font=w->font?w->font:d->view->font;
        if(!HmiFont_Valid(font))return Failure(d,HMI_INVALID);
        unsigned slot=0;while(slot<d->font_count&&d->fonts[slot].source!=font)++slot;
        if(slot==d->font_count) {
            if(slot>=4U||font->size>sizeof(d->font_pixels)-d->font_used)return Failure(d,HMI_INVALID);
            d->fonts[slot].source=font;d->fonts[slot].ram=*font;
            d->fonts[slot].ram.data=d->font_pixels+d->font_used;d->font_used+=font->size;++d->font_count;
        }
        if(!d->fonts[slot].ready) {
            d->font_slot=(uint8_t)slot;
            return Read(d,font->data,(uint8_t *)(uintptr_t)d->fonts[slot].ram.data,font->size,WAIT_FONT);
        }
        d->staged.font=&d->fonts[slot].ram;
        HmiResult r=HmiPages_DrawWidget(d->view,d->index,&d->staged,tile,s);
        if(r!=HMI_OK)return Failure(d,r);
        ++d->index;
    }
    d->busy=false;++d->tiles;return HMI_OK;
}
