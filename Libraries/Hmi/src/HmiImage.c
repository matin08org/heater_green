#include "HmiImage.h"
bool HmiImage2Aa_Valid(const HmiImage2Aa *i)
{
    return i&&i->data&&i->width&&i->height&&i->stride>=i->width&&
        (size_t)(i->height-1U)*i->stride+i->width<=i->size;
}
HmiResult HmiImage2Aa_Draw(HmiSurface *s,const HmiImage2Aa *i,int32_t x,int32_t y,const uint16_t c[4])
{
    if(!HmiSurface_Valid(s)||!HmiImage2Aa_Valid(i)||!c)return HMI_INVALID;
    uint16_t lut[256];
    for(unsigned n=0;n<256;n++) {
        uint16_t fg=c[n>>6],bg=c[(n>>4)&3U];unsigned a=n&15U,inv=15U-a;
        unsigned r=(((fg>>11)&31U)*a+((bg>>11)&31U)*inv+7U)/15U;
        unsigned g=(((fg>>5)&63U)*a+((bg>>5)&63U)*inv+7U)/15U;
        unsigned b=((fg&31U)*a+(bg&31U)*inv+7U)/15U;
        lut[n]=(uint16_t)((r<<11)|(g<<5)|b);
    }
    int64_t l=x<0?-(int64_t)x:0,t=y<0?-(int64_t)y:0,r=i->width,b=i->height;
    if((int64_t)x+r>s->width)r=(int64_t)s->width-x;
    if((int64_t)y+b>s->height)b=(int64_t)s->height-y;
    for(int64_t sy=t;sy<b;sy++) {
        const uint8_t *src=i->data+(size_t)sy*i->stride;
        uint16_t *dst=s->pixels+(size_t)(y+sy)*s->stride;
        for(int64_t sx=l;sx<r;sx++)dst[x+sx]=lut[src[sx]];
    }
    return HMI_OK;
}
void HmiPalette2_Set(HmiPalette2 *p, const uint16_t c[4])
{
    if (!p || !c) return;
    for (unsigned i = 0; i < 4; ++i) p->colors[i] = c[i];
    for (unsigned i = 0; i < 16; ++i) {
        p->pair[i][0] = p->colors[i >> 2];
        p->pair[i][1] = p->colors[i & 3U];
    }
}
bool HmiImage2_Valid(const HmiImage2 *i)
{
    return i && i->data && i->width && i->height &&
        i->stride >= ((uint32_t)i->width + 3U) / 4U &&
        (size_t)(i->height - 1U) * i->stride + ((uint32_t)i->width + 3U) / 4U <= i->size;
}
HmiResult HmiImage2_Draw(HmiSurface *s, const HmiImage2 *i, HmiRect r,
                       int32_t dx, int32_t dy, const HmiPalette2 *p)
{
    if (!HmiSurface_Valid(s) || !HmiImage2_Valid(i) || !p || r.x < 0 || r.y < 0 ||
        (int64_t)r.x + r.w > i->width || (int64_t)r.y + r.h > i->height) return HMI_INVALID;
    /* Clip using wide arithmetic, including INT32_MIN origins. */
    int64_t left = dx < 0 ? -(int64_t)dx : 0;
    int64_t top = dy < 0 ? -(int64_t)dy : 0;
    int64_t right = r.w, bottom = r.h;
    if ((int64_t)dx + right > s->width) right = (int64_t)s->width - dx;
    if ((int64_t)dy + bottom > s->height) bottom = (int64_t)s->height - dy;
    if (left >= right || top >= bottom) return HMI_OK;
    unsigned sx0 = (unsigned)(r.x + left), count = (unsigned)(right - left);
    for (int64_t row = top; row < bottom; ++row) {
        const uint8_t *src = i->data + (size_t)(r.y + row) * i->stride;
        uint16_t *dst = s->pixels + (size_t)(dy + row) * s->stride + (size_t)(dx + left);
        unsigned sx = sx0, n = count;
        /* Align to four source pixels; scalar head and tail handle crops. */
        while (n && (sx & 3U)) {
            *dst++ = p->colors[(src[sx >> 2] >> (6U - 2U * (sx & 3U))) & 3U];
            ++sx; --n;
        }
        src += sx >> 2;
        while (n >= 4U) {
            unsigned b = *src++;
            const uint16_t *hi = p->pair[b >> 4], *lo = p->pair[b & 15U];
            dst[0] = hi[0]; dst[1] = hi[1]; dst[2] = lo[0]; dst[3] = lo[1];
            dst += 4; n -= 4;
        }
        if (n) {
            unsigned b = *src;
            for (unsigned k = 0; k < n; ++k) *dst++ = p->colors[(b >> (6U - 2U * k)) & 3U];
        }
    }
    return HMI_OK;
}
