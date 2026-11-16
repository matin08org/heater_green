#include "HmiText.h"
bool HmiFont_Valid(const HmiFont *f)
{
    return f && f->data && f->coverage_bpp<=2U && f->proportional<=1U && f->first <= f->last && f->width && f->height &&
        f->advance >= f->width && f->line_height >= f->height &&
        (size_t)(f->last - f->first + 1U) * (f->height * ((f->width * (f->coverage_bpp==2U?2U:1U) + 7U) / 8U)+f->proportional) <= f->size;
}
static unsigned Advance(const HmiFont *f,uint8_t c)
{
    if(!f->proportional)return f->advance;
    if(c<f->first||c>f->last)c='?';
    return c<f->first||c>f->last?f->advance:f->data[c-f->first];
}
static uint16_t Blend(uint16_t bg,uint16_t fg,unsigned a)
{
    unsigned inv=3U-a;
    unsigned r=(((fg>>11)&31U)*a+((bg>>11)&31U)*inv+1U)/3U;
    unsigned g=(((fg>>5)&63U)*a+((bg>>5)&63U)*inv+1U)/3U;
    unsigned b=((fg&31U)*a+(bg&31U)*inv+1U)/3U;
    return (uint16_t)((r<<11)|(g<<5)|b);
}
HmiResult HmiText_Draw(HmiSurface *s, HmiRect field, const HmiFont *f,
                       const char *text, size_t cap, HmiTextStyle style)
{
    if (!HmiSurface_Valid(s) || !HmiFont_Valid(f) || !text || !cap ||
        (unsigned)style.align > HMI_TEXT_RIGHT) return HMI_INVALID;
    size_t len = 0;
    while (len < cap && len <= 65535U && text[len]) ++len;
    if (len == cap || len > 65535U) return HMI_INVALID;
    if (style.opaque) (void)HmiSurface_Fill(s, field, style.background);
    if (!field.w || !field.h || !len) return HMI_OK;
    int64_t width = (int64_t)(len - 1U) * f->advance + f->width;
    if(f->proportional) {
        width=0;
        for(size_t k=0;k<len;k++) {
            unsigned a=Advance(f,(uint8_t)text[k]);
            if(!a||a>f->width)return HMI_INVALID;
            width+=a;
        }
    }
    int64_t x0 = field.x;
    if (style.align == HMI_TEXT_CENTER) x0 += ((int64_t)field.w - width) / 2;
    if (style.align == HMI_TEXT_RIGHT) x0 += (int64_t)field.w - width;
    int64_t left = field.x < 0 ? 0 : field.x, top = field.y < 0 ? 0 : field.y;
    int64_t right = (int64_t)field.x + field.w, bottom = (int64_t)field.y + field.h;
    if (right > s->width) right = s->width;
    if (bottom > s->height) bottom = s->height;
    if (left >= right || top >= bottom) return HMI_OK;
    unsigned bpp=f->coverage_bpp==2U?2U:1U;
    size_t row_bytes = (f->width*bpp + 7U) / 8U, glyph_bytes = row_bytes * f->height;
    int64_t pen=x0;
    for (size_t k = 0; k < len; ++k) {
        int64_t gx=pen;pen+=Advance(f,(uint8_t)text[k]);
        if (gx >= right) break;
        if (gx + f->width <= left) continue;
        uint8_t c = (uint8_t)text[k];
        if (c < f->first || c > f->last) c = (uint8_t)'?';
        if (c < f->first || c > f->last) continue;
        const uint8_t *glyph = f->data + (f->proportional?(size_t)(f->last-f->first+1U):0U) + (size_t)(c - f->first) * glyph_bytes;
        for (unsigned gy = 0; gy < f->height; ++gy) {
            int64_t y = (int64_t)field.y + gy;
            if (y < top || y >= bottom) continue;
            uint16_t *dst = s->pixels + (size_t)y * s->stride;
            for (unsigned bx = 0; bx < f->width; ++bx) {
                int64_t x = gx + bx;
                if(x<left||x>=right)continue;
                unsigned a=bpp==2U?(glyph[gy*row_bytes+bx/4U]>>(6U-2U*(bx&3U)))&3U:
                    (glyph[gy*row_bytes+bx/8U]&(0x80U>>(bx&7U)))?3U:0U;
                if(a)dst[x]=a==3U?style.foreground:Blend(dst[x],style.foreground,a);
            }
        }
    }
    return HMI_OK;
}
HmiResult HmiText_U32(char *dst, size_t cap, uint32_t value)
{
    char reverse[10]; unsigned n = 0;
    if (!dst || !cap) return HMI_INVALID;
    do { reverse[n++] = (char)('0' + value % 10U); value /= 10U; } while (value);
    if (cap <= n) { dst[0] = '\0'; return HMI_INVALID; }
    for (unsigned k = 0; k < n; ++k) dst[k] = reverse[n - k - 1U];
    dst[n] = '\0'; return HMI_OK;
}
HmiResult HmiText_Q4(char *dst, size_t cap, int32_t value)
{
    if (!dst || !cap) return HMI_INVALID;
    /* Unsigned subtraction also handles INT32_MIN without signed overflow. */
    uint32_t magnitude = value < 0 ? 0U - (uint32_t)value : (uint32_t)value;
    char number[11];
    (void)HmiText_U32(number, sizeof(number), magnitude / 4U);
    size_t n = 0; while (number[n]) ++n;
    size_t sign = value < 0 ? 1U : 0U;
    if (cap < sign + n + 4U) { dst[0] = '\0'; return HMI_INVALID; }
    if (sign) dst[0] = '-';
    for (size_t k = 0; k < n; ++k) dst[sign + k] = number[k];
    unsigned fraction = (magnitude & 3U) * 25U;
    dst[sign + n] = '.'; dst[sign + n + 1U] = (char)('0' + fraction / 10U);
    dst[sign + n + 2U] = (char)('0' + fraction % 10U); dst[sign + n + 3U] = '\0';
    return HMI_OK;
}
