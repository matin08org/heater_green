#include "HmiTypes.h"
bool HmiSurface_Valid(const HmiSurface *s)
{
    return s && s->pixels && s->width && s->height && s->stride >= s->width &&
        (size_t)(s->height - 1U) * s->stride + s->width <= s->capacity;
}
HmiResult HmiSurface_Fill(HmiSurface *s, HmiRect r, uint16_t color)
{
    if (!HmiSurface_Valid(s)) return HMI_INVALID;
    int64_t right = (int64_t)r.x + r.w, bottom = (int64_t)r.y + r.h;
    int32_t x = r.x < 0 ? 0 : r.x, y = r.y < 0 ? 0 : r.y;
    if (right > s->width) right = s->width;
    if (bottom > s->height) bottom = s->height;
    for (; y < bottom; ++y) {
        uint16_t *p = s->pixels + (size_t)y * s->stride;
        for (int32_t i = x; i < right; ++i) p[i] = color;
    }
    return HMI_OK;
}
