#ifndef HMI_PAGES_DMA_H
#define HMI_PAGES_DMA_H
#include "HmiPages.h"
typedef struct {
    void *context;
    /* OK accepts ownership, BUSY accepts none. poll OK releases destination. */
    HmiResult (*start)(void *,const uint8_t *source,uint8_t *destination,size_t bytes);
    HmiResult (*poll)(void *);
} HmiAssetIo;
typedef struct {
    const HmiPageView *view;
    HmiAssetIo io;
    HmiRect tile;
    uint16_t *pixels;
    uint16_t index,row,end_row;
    uint8_t phase,font_slot,font_count;
    bool busy;
    HmiWidget staged;
    HmiImage2 image;
    HmiImage2Aa aa;
    HmiRgbImage rgb;
    struct {const HmiFont *source;HmiFont ram;bool ready;} fonts[4];
    size_t font_used;
    uint8_t font_pixels[20480]; /* cached AA labels + bold numeric glyphs */
    uint8_t scratch[2048];
    uint32_t reads,bytes,tiles;
    HmiResult error;
} HmiPagesDma;
HmiResult HmiPagesDma_Init(HmiPagesDma *renderer,HmiAssetIo io,const HmiPageView *view);
const HmiFont *HmiPagesDma_CachedFont(const HmiPagesDma *renderer,const HmiFont *source);
/* Resume on the same immutable tile until OK. Never waits for DMA in a loop.
 * Font bitmaps are DMA-cached once; each image strip is fetched asynchronously.
 * Small descriptors/page metadata remain CPU-readable in memory-mapped flash. */
HmiResult HmiPagesDma_Compose(void *renderer,HmiRect tile,HmiSurface *surface);
#endif
