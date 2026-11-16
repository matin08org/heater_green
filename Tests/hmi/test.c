#include "HmiImage.h"
#include "HmiText.h"
#include "HmiStream.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint32_t random_state = 0x13679U;
static uint32_t Next(void) { random_state ^= random_state << 13; random_state ^= random_state >> 17; random_state ^= random_state << 5; return random_state; }
static void Images(void)
{
    uint8_t input[256];
    uint16_t actual[500], expected[500];
    HmiPalette2 palette;
    uint16_t colors[] = {0, 0x1234, 0xabcd, 0xffff};
    HmiPalette2_Set(&palette, colors);
    for (unsigned trial = 0; trial < 20000; ++trial) {
        unsigned w = 1 + Next() % 39U, h = 1 + Next() % 9U;
        unsigned stride = (w + 3U) / 4U + Next() % 3U;
        for (unsigned n = 0; n < sizeof(input); ++n) input[n] = (uint8_t)Next();
        HmiImage2 image = {input, stride * h, (uint16_t)w, (uint16_t)h, (uint16_t)stride};
        HmiSurface surface = {actual + 8, 22 * 11, 19, 11, 22};
        for (unsigned n = 0; n < 500; ++n) actual[n] = expected[n] = (uint16_t)Next();
        unsigned sx = Next() % w, sy = Next() % h;
        HmiRect r = {(int32_t)sx, (int32_t)sy, (uint16_t)(1 + Next() % (w - sx)), (uint16_t)(1 + Next() % (h - sy))};
        int dx = (int)(Next() % 40U) - 15, dy = (int)(Next() % 22U) - 8;
        for (unsigned y = 0; y < r.h; ++y) for (unsigned x = 0; x < r.w; ++x) {
            int xx = dx + (int)x, yy = dy + (int)y;
            if (xx >= 0 && xx < 19 && yy >= 0 && yy < 11) {
                unsigned ix = sx + x;
                unsigned index = (input[(sy + y) * stride + ix / 4U] >> (6U - 2U * (ix & 3U))) & 3U;
                expected[8 + yy * 22 + xx] = colors[index];
            }
        }
        assert(HmiImage2_Draw(&surface, &image, r, dx, dy, &palette) == HMI_OK);
        assert(!memcmp(actual, expected, sizeof(actual)));
        assert(HmiImage2_Draw(&surface, &image, r, INT32_MIN, INT32_MAX, &palette) == HMI_OK);
    }
    uint8_t bytes[] = {0x1b, 0xe4}; uint16_t out[8];
    HmiSurface s = {out,8,8,1,8}; HmiImage2 i = {bytes,2,8,1,2};
    assert(HmiImage2_Draw(&s,&i,(HmiRect){0,0,8,1},0,0,&palette)==HMI_OK);
    colors[0] = 0x4567; HmiPalette2_Set(&palette,colors);
    assert(HmiImage2_Draw(&s,&i,(HmiRect){0,0,8,1},0,0,&palette)==HMI_OK && out[0]==0x4567);
    i.size=1; assert(!HmiImage2_Valid(&i)); i.size=2; i.stride=1; assert(!HmiImage2_Valid(&i));
    i.stride=2; assert(HmiImage2_Draw(&s,&i,(HmiRect){7,0,2,1},0,0,&palette)==HMI_INVALID);
    s.capacity=7; assert(!HmiSurface_Valid(&s));
    puts("PASS image: 20000 randomized crops, unaligned rows, padding, palette changes, clipping and guards");
}
static void Text(void)
{
    uint8_t glyphs[95 * 7];
    for(unsigned n=0;n<sizeof(glyphs);++n) glyphs[n]=(uint8_t)Next();
    HmiFont font = {glyphs,sizeof(glyphs),32,126,5,7,6,8,1,0};
    assert(HmiFont_Valid(&font));
    uint16_t full[80*20], tiled[80*20], initial[80*20];
    const char *texts[]={"1234", "99", "CO2: 700", "", "A\xff?"};
    for(unsigned mode=0;mode<6;++mode) for(unsigned text=0;text<5;++text) {
        for(unsigned p=0;p<80*20;++p) full[p]=tiled[p]=initial[p]=(uint16_t)Next();
        HmiTextStyle style={0xf800,0x1234,(HmiTextAlign)(mode%3),mode>=3};
        HmiRect field={-3,2,70,15};
        HmiSurface s={full,80*20,80,20,80};
        assert(HmiText_Draw(&s,field,&font,texts[text],32,style)==HMI_OK);
        for(unsigned y=0;y<20;y+=3) {
            unsigned h=20-y<3?20-y:3;
            HmiSurface tile={tiled+y*80,h*80,80,(uint16_t)h,80};
            HmiRect local=field;local.y-=(int32_t)y;
            assert(HmiText_Draw(&tile,local,&font,texts[text],32,style)==HMI_OK);
        }
        assert(!memcmp(full,tiled,sizeof(full)));
        if(!style.opaque) for(unsigned p=0;p<80*20;++p)
            assert(full[p]==initial[p] || full[p]==style.foreground);
    }
    HmiSurface s={full,80*20,80,20,80};
    HmiTextStyle opaque={1,2,HMI_TEXT_LEFT,true};
    memset(full,0,sizeof(full));
    assert(HmiText_Draw(&s,(HmiRect){0,0,70,10},&font,"1000",5,opaque)==HMI_OK);
    assert(HmiText_Draw(&s,(HmiRect){0,0,70,10},&font,"9",2,opaque)==HMI_OK);
    for(unsigned y=0;y<10;++y) for(unsigned x=5;x<70;++x) assert(full[y*80+x]==2);
    char bad[]={'x','y'};
    assert(HmiText_Draw(&s,(HmiRect){0,0,70,10},&font,bad,2,opaque)==HMI_INVALID);
    char number[20];
    assert(HmiText_U32(number,sizeof(number),UINT32_MAX)==HMI_OK && !strcmp(number,"4294967295"));
    assert(HmiText_Q4(number,sizeof(number),INT32_MIN)==HMI_OK && !strcmp(number,"-536870912.00"));
    assert(HmiText_Q4(number,sizeof(number),-1)==HMI_OK && !strcmp(number,"-0.25"));
    assert(HmiText_Q4(number,sizeof(number),2801)==HMI_OK && !strcmp(number,"700.25"));
    assert(HmiText_Q4(number,6,2801)==HMI_INVALID && !number[0]);
    const uint8_t coverage[]={0x1b}; /* exactly 0, 1/3, 2/3, 1 */
    HmiFont aa={coverage,sizeof(coverage),'?','?',4,1,4,1,2,0};
    uint16_t pixels[]={0x001f,0x001f,0x001f,0x001f};
    HmiSurface as={pixels,4,4,1,4};
    HmiTextStyle blend={0xf800,0,HMI_TEXT_LEFT,false};
    assert(HmiText_Draw(&as,(HmiRect){0,0,4,1},&aa,"?",2,blend)==HMI_OK);
    assert(pixels[0]==0x001f&&pixels[1]==0x5015&&pixels[2]==0xa80a&&pixels[3]==0xf800);
    aa.size=0;assert(!HmiFont_Valid(&aa));aa.size=1;aa.coverage_bpp=3;assert(!HmiFont_Valid(&aa));
    uint8_t aa_glyphs[95*7*2];for(unsigned i=0;i<sizeof(aa_glyphs);i++)aa_glyphs[i]=(uint8_t)Next();
    aa=(HmiFont){aa_glyphs,sizeof(aa_glyphs),32,126,5,7,6,8,2,0};
    for(unsigned align=0;align<3;align++) {
        for(unsigned p=0;p<80*20;p++)full[p]=tiled[p]=(uint16_t)Next();
        HmiTextStyle astyle={0xffff,0,(HmiTextAlign)align,false};
        assert(HmiText_Draw(&s,(HmiRect){-3,2,70,15},&aa,"CO2: 123",9,astyle)==HMI_OK);
        for(unsigned y=0;y<20;y+=2) {
            HmiSurface tile={tiled+y*80,160,80,2,80};
            assert(HmiText_Draw(&tile,(HmiRect){-3,2-(int)y,70,15},&aa,"CO2: 123",9,astyle)==HMI_OK);
        }
        assert(!memcmp(full,tiled,sizeof(full)));
    }
    puts("PASS antialiased text: exact 4-level RGB565 coverage, transparent background blending, bounds/format and tile equivalence");
    const uint8_t prop[]={2,4,0xf0,0xff};
    HmiFont pf={prop,sizeof(prop),'?','@',4,1,4,1,2,1};
    uint16_t row[8]={0};HmiSurface ps={row,8,8,1,8};
    HmiTextStyle pstyle={0xffff,0,HMI_TEXT_CENTER,false};
    assert(HmiText_Draw(&ps,(HmiRect){0,0,8,1},&pf,"?@",3,pstyle)==HMI_OK);
    assert(row[0]==0&&row[7]==0);for(unsigned i=1;i<7;i++)assert(row[i]==0xffff);
    puts("PASS proportional glyph advances: centered layout and cached prefix format");
    puts("PASS text: tile equivalence, three alignments, transparency, erase old digits, bounded strings and Q4 extremes");
}
typedef struct {
    bool busy, refuse, fail;
    unsigned polls, starts, composed;
    HmiRect tile;
    const uint16_t *owned;
    uint16_t saved[40], screen[100];
} Fake;
static HmiResult Compose(void *context,HmiRect r,HmiSurface *s)
{
    Fake *f=context;++f->composed;
    assert(s->pixels!=f->owned);
    for(unsigned y=0;y<s->height;++y) for(unsigned x=0;x<s->width;++x)
        s->pixels[y*s->stride+x]=(uint16_t)((r.y+(int)y)*10+r.x+(int)x);
    return HMI_OK;
}
static HmiResult Start(void *context,HmiRect r,const uint16_t *pixels)
{
    Fake *f=context;assert(!f->busy);
    if(f->refuse) return HMI_BUSY;
    f->busy=true;f->tile=r;f->owned=pixels;f->polls=0;++f->starts;
    memcpy(f->saved,pixels,r.w*r.h*2U);return HMI_OK;
}
static HmiResult Poll(void *context)
{
    Fake *f=context;
    if(f->fail) return HMI_IO_ERROR;
    if(!f->busy) return HMI_OK;
    assert(!memcmp(f->saved,f->owned,f->tile.w*f->tile.h*2U));
    if(++f->polls<4) return HMI_BUSY;
    for(unsigned y=0;y<f->tile.h;++y) for(unsigned x=0;x<f->tile.w;++x)
        f->screen[(f->tile.y+(int)y)*10+f->tile.x+(int)x]=f->owned[y*f->tile.w+x];
    f->busy=false;f->owned=NULL;return HMI_OK;
}
static void Streaming(void)
{
    Fake f={0};uint16_t a[40],b[40];HmiStream s;
    HmiTransport transport={&f,Start,Poll};
    assert(HmiStream_Init(&s,transport,10,10,a,a,40)==HMI_INVALID);
    assert(HmiStream_Init(&s,transport,10,10,a,b,9)==HMI_INVALID);
    assert(HmiStream_Init(&s,transport,10,10,a,b,40)==HMI_OK);
    assert(HmiStream_Submit(&s,(HmiRect){2,1,7,8},Compose,&f)==HMI_OK);
    assert(HmiStream_Submit(&s,(HmiRect){0,0,1,1},Compose,&f)==HMI_BUSY);
    f.refuse=true;
    for(unsigned n=0;n<10;++n) {assert(HmiStream_Process(&s)==HMI_BUSY);assert(f.composed==1);}
    f.refuse=false;
    for(unsigned n=0;s.busy && n<100;++n) (void)HmiStream_Process(&s);
    assert(!s.busy && s.completed_jobs==1 && s.completed_tiles==2 && f.starts==2);
    for(unsigned y=1;y<9;++y) for(unsigned x=2;x<9;++x) assert(f.screen[y*10+x]==y*10+x);
    assert(HmiStream_Submit(&s,(HmiRect){0,0,10,10},Compose,&f)==HMI_OK);
    (void)HmiStream_Process(&s);
    unsigned starts=f.starts;
    HmiStream_Cancel(&s);
    for(unsigned n=0;s.busy && n<30;++n)(void)HmiStream_Process(&s);
    assert(!s.busy && f.starts==starts && !f.busy && !s.cancel_requested);
    assert(HmiStream_Submit(&s,(HmiRect){9,9,2,2},Compose,&f)==HMI_INVALID);
    assert(HmiStream_Submit(&s,(HmiRect){0,0,10,10},Compose,&f)==HMI_OK);
    (void)HmiStream_Process(&s);f.fail=true;
    assert(HmiStream_Process(&s)==HMI_IO_ERROR && s.busy);
    assert(HmiStream_Process(&s)==HMI_IO_ERROR);
    assert(HmiStream_Submit(&s,(HmiRect){0,0,1,1},Compose,&f)==HMI_IO_ERROR);
    puts("PASS stream: exact dirty region, DMA buffer ownership, double-buffer overlap, backpressure and latched errors");
}
static void AntialiasedImages(void)
{
    uint8_t bytes[256];for(unsigned n=0;n<256;n++)bytes[n]=(uint8_t)n;
    HmiImage2Aa im={bytes,sizeof(bytes),32,8,32};
    uint16_t palette[]={0xf800,0x001f,0x07e0,0xffff};
    uint16_t full[34*10],tiled[34*10];
    for(unsigned n=0;n<34*10;n++)full[n]=tiled[n]=0x1357;
    HmiSurface s={full,34*10,34,10,34};
    assert(HmiImage2Aa_Draw(&s,&im,1,1,palette)==HMI_OK);
    assert(full[34+1+0x10]==0x001f&&full[34+1+0x15]==0x5015&&
           full[34+1+0x1a]==0xa80a&&full[34+1+0x1f]==0xf800);
    for(unsigned y=0;y<10;y+=2) {
        HmiSurface tile={tiled+y*34,68,34,2,34};
        assert(HmiImage2Aa_Draw(&tile,&im,1,1-(int)y,palette)==HMI_OK);
    }
    assert(!memcmp(full,tiled,sizeof(full)));
    for(unsigned x=0;x<34;x++)assert(full[x]==0x1357&&full[9*34+x]==0x1357);
    for(unsigned y=0;y<10;y++)assert(full[y*34]==0x1357&&full[y*34+33]==0x1357);
    im.size=255;assert(!HmiImage2Aa_Valid(&im));im.size=256;im.stride=31;assert(!HmiImage2Aa_Valid(&im));
    im.stride=32;assert(HmiImage2Aa_Draw(&s,&im,INT32_MIN,INT32_MIN,palette)==HMI_OK);
    palette[0]=0xffff;assert(HmiImage2Aa_Draw(&s,&im,0,0,palette)==HMI_OK);
    assert(full[0x1f]==0xffff&&full[0x10]==0x001f);
    puts("PASS AA icons/panels: 256 blend codes, exact coverage endpoints, independent recolor, clipping/guards and tile equivalence");
}
int main(void) { Images();AntialiasedImages();Text();Streaming();return 0; }
