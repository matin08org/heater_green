#ifndef HMI_TEXT_H
#define HMI_TEXT_H
#include "HmiTypes.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Glyph cells have height rows, ceil(width*coverage_bpp/8) bytes per row.
 * Binary masks use MSB bits; antialiased masks use MSB pairs 0..3. Optional
 * proportional advances occupy a glyph-count-byte prefix in the SAME data
 * block, so the DMA font cache owns metrics and pixels together. Missing
 * characters use '?' or blank. No heap or framebuffer is required. */
typedef struct {
    const uint8_t *data;
    size_t size;
    uint8_t first, last, width, height, advance, line_height;
    uint8_t coverage_bpp; /* 0/1: binary MSB mask; 2: four coverage levels, MSB pairs */
    uint8_t proportional; /* data begins with one advance byte per glyph when set */
} HmiFont;
typedef enum { HMI_TEXT_LEFT, HMI_TEXT_CENTER, HMI_TEXT_RIGHT } HmiTextAlign;
typedef struct {
    uint16_t foreground, background;
    HmiTextAlign align;
    bool opaque; /* fill the ENTIRE field first; false preserves tile pixels */
} HmiTextStyle;
bool HmiFont_Valid(const HmiFont *font);
/* Bounded string scan; one line. Out-of-field glyphs are clipped, never wrapped.
 * field may extend outside tile: keep original field coordinates for alignment.
 * capacity includes the NUL byte; an unterminated string is rejected. */
HmiResult HmiText_Draw(HmiSurface *surface, HmiRect field, const HmiFont *font,
                       const char *text, size_t capacity, HmiTextStyle style);
/* Small numeric helpers: no printf, float, heap or locale. On overflow,
 * dst[0] is cleared. Q4 is temperature in quarter-degrees, e.g. 2801 -> 700.25. */
HmiResult HmiText_U32(char *dst, size_t capacity, uint32_t value);
HmiResult HmiText_Q4(char *dst, size_t capacity, int32_t value);
#ifdef __cplusplus
}
#endif
#endif
