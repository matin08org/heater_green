#include "HmiPagesDma.h"
#include "HmiStream.h"
#include "HmiHome.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern const HmiFont monitor_font,home_small_font,home_value_font;
static uint16_t expected[480*320],actual[480*320];
static HmiPagesDma renderer;
#ifndef HMI_TEST_TILE_ROWS
#define HMI_TEST_TILE_ROWS 2U
#endif
#define TEST_PIXELS (480U*HMI_TEST_TILE_ROWS)
typedef struct {
    bool reading,sending,read_error,start_error,refuse;
    const uint8_t *source;uint8_t *destination;size_t bytes;
    const uint16_t *owned;uint16_t saved[TEST_PIXELS];HmiRect tile;
    unsigned read_polls,send_polls,overlap,starts,font_reads,read_count;
} Fake;
static HmiResult Read(void *ctx,const uint8_t *src,uint8_t *dst,size_t bytes)
{
    Fake *f=ctx;assert(!f->reading&&src&&dst&&bytes);
    if(f->start_error)return HMI_IO_ERROR;
    if(f->refuse)return HMI_BUSY;
    f->reading=true;f->source=src;f->destination=dst;f->bytes=bytes;f->read_polls=0;++f->read_count;
    if(src==monitor_font.data||src==home_small_font.data||src==home_value_font.data)++f->font_reads;
    if(f->sending)++f->overlap;
    return HMI_OK;
}
static HmiResult ReadPoll(void *ctx)
{
    Fake *f=ctx;if(f->read_error){f->reading=false;return HMI_IO_ERROR;}
    if(!f->reading)return HMI_OK;
    if(++f->read_polls<3)return HMI_BUSY;
    memcpy(f->destination,f->source,f->bytes);f->reading=false;return HMI_OK;
}
static HmiResult Start(void *ctx,HmiRect tile,const uint16_t *pixels)
{
    Fake *f=ctx;assert(!f->sending);assert(tile.w*tile.h<=TEST_PIXELS);
    f->sending=true;f->owned=pixels;f->tile=tile;f->send_polls=0;++f->starts;
    memcpy(f->saved,pixels,(size_t)tile.w*tile.h*2U);return HMI_OK;
}
static HmiResult Poll(void *ctx)
{
    Fake *f=ctx;if(!f->sending)return HMI_OK;
    assert(!memcmp(f->owned,f->saved,(size_t)f->tile.w*f->tile.h*2U));
    if(++f->send_polls<20)return HMI_BUSY;
    for(unsigned y=0;y<f->tile.h;++y)
        memcpy(actual+((unsigned)f->tile.y+y)*480U+(unsigned)f->tile.x,
            f->owned+(size_t)y*f->tile.w,(size_t)f->tile.w*2U);
    f->sending=false;f->owned=NULL;return HMI_OK;
}
static void Run(HmiStream *stream,Fake *f)
{
    unsigned n=0;
    while(stream->busy&&n++<200000U) {
        assert(HmiStream_Process(stream)!=HMI_IO_ERROR);
        if(f->sending)assert(!memcmp(f->owned,f->saved,(size_t)f->tile.w*f->tile.h*2U));
    }
    assert(!stream->busy&&!f->reading&&!f->sending);
}
int main(void)
{
    HmiPageView view={.page=HmiPages_Find(hmi_pages,hmi_page_count,"home"),.font=&monitor_font,.focus=-1};
    HmiHomeState state={.running=true,.configured=true,.co_valid=true,.co_ppm=4,.co_limit_ppm=10,
        .co_accept_ppm=5,.co2_valid=true,.co2_ppm=670,.target_ppm=800,.process_proven=true};
    HmiHome_Apply(&view,&state);strcpy(view.values[HMI_BIND_TC1],"765.25 C");strcpy(view.values[HMI_BIND_TC2],"39.50 C");
    HmiSurface full={expected,480*320,480,320,480};
    assert(HmiPages_Compose(&view,(HmiRect){0,0,480,320},&full)==HMI_OK);
    Fake f={0};HmiStream stream;uint16_t first[TEST_PIXELS],second[TEST_PIXELS];
    HmiAssetIo io={&f,Read,ReadPoll};
    assert(HmiPagesDma_Init(&renderer,io,&view)==HMI_OK);
    assert(HmiStream_Init(&stream,(HmiTransport){&f,Start,Poll},480,320,first,second,TEST_PIXELS)==HMI_OK);
    assert(HmiStream_Submit(&stream,(HmiRect){0,0,480,320},HmiPagesDma_Compose,&renderer)==HMI_OK);
    Run(&stream,&f);assert(!memcmp(expected,actual,sizeof(actual)));
    assert(f.starts==(320U+HMI_TEST_TILE_ROWS-1U)/HMI_TEST_TILE_ROWS);
    printf("PASS full page: %u LCD tiles at %u rows; pixel-exact output\n",f.starts,HMI_TEST_TILE_ROWS);
    assert(f.overlap>0&&f.font_reads==3&&renderer.font_count==3);
    unsigned font_reads=f.font_reads;
    /* Narrow, tall dirty windows exercise chunked staging and unaligned rows. */
    memset(actual,0,sizeof(actual));
    assert(HmiStream_Submit(&stream,(HmiRect){79,0,1,320},HmiPagesDma_Compose,&renderer)==HMI_OK);
    Run(&stream,&f);for(unsigned y=0;y<320;++y)assert(actual[y*480+79]==expected[y*480+79]);
    assert(f.font_reads==font_reads);
    /* Backpressure cannot turn a refused read into a ready RGB tile. */
    f.refuse=true;unsigned starts=f.starts;
    assert(HmiStream_Submit(&stream,(HmiRect){0,3,480,20},HmiPagesDma_Compose,&renderer)==HMI_OK);
    for(unsigned n=0;n<10;++n)assert(HmiStream_Process(&stream)==HMI_BUSY);
    assert(f.starts==starts&&!f.reading);f.refuse=false;
    assert(HmiStream_Process(&stream)==HMI_BUSY&&f.reading);
    /* Page cancellation must drain a read before the source snapshot is reusable. */
    HmiStream_Cancel(&stream);Run(&stream,&f);assert(f.starts==starts&&!renderer.busy);
    /* Same stream can render the next page/job after cancellation. */
    assert(HmiStream_Submit(&stream,(HmiRect){7,113,163,29},HmiPagesDma_Compose,&renderer)==HMI_OK);
    Run(&stream,&f);assert(!renderer.busy);
    /* Mid-read failure is latched and never submits partial pixels. */
    starts=f.starts;
    assert(HmiStream_Submit(&stream,(HmiRect){0,0,480,20},HmiPagesDma_Compose,&renderer)==HMI_OK);
    for(unsigned n=0;n<30&&!(f.reading&&f.sending);++n)assert(HmiStream_Process(&stream)==HMI_BUSY);
    assert(f.reading&&f.sending);starts=f.starts;f.read_error=true;
    assert(HmiStream_Process(&stream)==HMI_IO_ERROR&&stream.busy);
    assert(f.starts==starts&&HmiStream_Process(&stream)==HMI_IO_ERROR);
    /* Recover the healthy LCD only AFTER its owned buffer has drained. A
       synchronous/internal-font diagnostic compositor needs no asset DMA. */
    unsigned drain=0;while(Poll(&f)==HMI_BUSY)assert(++drain<30);
    assert(!f.reading&&!f.sending);
    assert(HmiStream_Init(&stream,(HmiTransport){&f,Start,Poll},480,320,first,second,TEST_PIXELS)==HMI_OK);
    assert(HmiStream_Submit(&stream,(HmiRect){0,0,480,20},HmiPages_Compose,&view)==HMI_OK);
    Run(&stream,&f);assert(stream.completed_jobs==1);
    puts("PASS asset DMA pipeline: pixel equivalence, overlap/ownership, fonts, crops, cancel, error drain and LCD recovery without asset DMA");
    return 0;
}
