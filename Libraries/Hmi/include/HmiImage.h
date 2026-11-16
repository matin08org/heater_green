#ifndef HMI_IMAGE_H
#define HMI_IMAGE_H
#include "HmiTypes.h"
#ifdef __cplusplus
extern "C" {
#endif
/* I2: top-down rows, left-to-right pixels; first pixel in bits 7:6.
 * Every row starts at a byte boundary. Unused low bits are ignored.
 * Index 0 is opaque. data may point directly into memory-mapped QSPI. */
typedef struct {
    const uint8_t *data;
    size_t size;
    uint16_t width, height, stride; /* stride in BYTES, >= ceil(width/4) */
} HmiImage2;
/* Antialiased four-role image. One byte/pixel: foreground role [7:6],
 * underlying role [5:4], coverage [3:0]. Both roles still index the SAME four
 * independently mutable colors; no RGB bitmap or baked palette is stored. */
typedef struct {const uint8_t *data;size_t size;uint16_t width,height,stride;} HmiImage2Aa;
bool HmiImage2Aa_Valid(const HmiImage2Aa *image);
HmiResult HmiImage2Aa_Draw(HmiSurface *surface,const HmiImage2Aa *image,
                         int32_t x,int32_t y,const uint16_t colors[4]);
/* 16 pairs expand a nibble to two pixels. Small enough for a per-widget
 * palette; no 2KB table rebuild or unaligned word reads required. */
typedef struct { uint16_t colors[4]; uint16_t pair[16][2]; } HmiPalette2;
void HmiPalette2_Set(HmiPalette2 *palette, const uint16_t colors[4]);
bool HmiImage2_Valid(const HmiImage2 *image);
/* source must be entirely inside image. Destination is clipped to surface.
 * sx may be unaligned to a byte. No access outside source/destination. */
HmiResult HmiImage2_Draw(HmiSurface *surface, const HmiImage2 *image,
                       HmiRect source, int32_t dx, int32_t dy,
                       const HmiPalette2 *palette);
#ifdef __cplusplus
}
#endif
#endif
