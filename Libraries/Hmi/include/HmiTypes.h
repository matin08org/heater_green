#ifndef HMI_TYPES_H
#define HMI_TYPES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* All pixels are native uint16_t RGB565 values, not pre-swapped SPI bytes.
 * Buffers and inputs must not overlap. Calls run in the main/task context. */
typedef enum { HMI_OK, HMI_BUSY, HMI_INVALID, HMI_IO_ERROR } HmiResult;
typedef struct { int32_t x, y; uint16_t w, h; } HmiRect;
typedef struct {
    uint16_t *pixels;
    size_t capacity;                 /* uint16_t elements, including row gaps */
    uint16_t width, height, stride;  /* stride in pixels */
} HmiSurface;
bool HmiSurface_Valid(const HmiSurface *surface);
HmiResult HmiSurface_Fill(HmiSurface *surface, HmiRect rectangle, uint16_t color);
static inline uint16_t Hmi_RGB565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xf8U) << 8) |
                      ((uint16_t)(g & 0xfcU) << 3) | (b >> 3));
}
#ifdef __cplusplus
}
#endif
#endif
