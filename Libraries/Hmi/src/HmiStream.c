#include "HmiStream.h"
#include <string.h>
HmiResult HmiStream_Init(HmiStream *s, HmiTransport t, uint16_t w, uint16_t h,
                        uint16_t *a, uint16_t *b, size_t pixels)
{
    if (!s || !t.start || !t.poll || !w || !h || !a || !b || pixels < w ||
        pixels > UINT16_MAX || pixels > SIZE_MAX / sizeof(uint16_t)) return HMI_INVALID;
    uintptr_t aa = (uintptr_t)a, bb = (uintptr_t)b;
    size_t bytes = pixels * sizeof(uint16_t);
    if ((aa & 1U) || (bb & 1U) || aa > UINTPTR_MAX - bytes || bb > UINTPTR_MAX - bytes ||
        (aa < bb + bytes && bb < aa + bytes)) return HMI_INVALID;
    memset(s, 0, sizeof(*s));
    s->transport = t; s->screen_width = w; s->screen_height = h;
    s->buffers[0] = a; s->buffers[1] = b; s->buffer_pixels = pixels;
    s->active = s->prepared = s->composing = -1;
    return HMI_OK;
}
HmiResult HmiStream_Submit(HmiStream *s, HmiRect r, HmiCompose compose, void *snapshot)
{
    if (!s || !compose || !s->transport.start || !s->transport.poll) return HMI_INVALID;
    if (s->error) return s->error;
    if (s->busy) return HMI_BUSY;
    if (!r.w || !r.h || r.x < 0 || r.y < 0 ||
        (int64_t)r.x + r.w > s->screen_width ||
        (int64_t)r.y + r.h > s->screen_height) return HMI_INVALID;
    s->job = r; s->compose = compose; s->snapshot = snapshot;
    s->next_row = 0; s->busy = true;
    return HMI_OK;
}
void HmiStream_Cancel(HmiStream *s) { if(s && s->busy)s->cancel_requested=true; }
static HmiResult Fault(HmiStream *s, HmiResult error)
{
    s->error = error == HMI_INVALID ? HMI_INVALID : HMI_IO_ERROR;
    /* Remain busy: a driver error may still own the DMA buffer. */
    return s->error;
}
HmiResult HmiStream_Process(HmiStream *s)
{
    if (!s || !s->transport.start || !s->transport.poll) return HMI_INVALID;
    if (s->error) return s->error;
    /* Always service driver, including its nonblocking panel initialization. */
    HmiResult result = s->transport.poll(s->transport.context);
    if (result != HMI_OK && result != HMI_BUSY) return Fault(s, result);
    if (s->active >= 0 && result == HMI_OK) {
        s->active = -1; ++s->completed_tiles;
    }
    if (!s->busy) return HMI_OK;
    if(s->cancel_requested) {
        s->prepared=-1;
        if(s->composing>=0) {
            int index=s->composing;
            HmiSurface surface={s->buffers[index],s->buffer_pixels,s->job.w,s->tiles[index].h,s->job.w};
            HmiResult drained=s->compose(s->snapshot,s->tiles[index],&surface);
            if(drained!=HMI_OK&&drained!=HMI_BUSY)return Fault(s,drained);
            if(drained==HMI_OK)s->composing=-1;
        }
        if(s->active<0&&s->composing<0) {s->busy=false;s->cancel_requested=false;return HMI_OK;}
        return HMI_BUSY;
    }
    if (s->prepared < 0 && s->next_row < s->job.h) {
        if(s->composing<0) {
            int free_index=s->active==0?1:0;
            size_t rows=s->buffer_pixels/s->job.w;
            if(rows>(size_t)(s->job.h-s->next_row))rows=s->job.h-s->next_row;
            s->tiles[free_index]=(HmiRect){s->job.x,s->job.y+s->next_row,s->job.w,(uint16_t)rows};
            s->composing=(int8_t)free_index;
        }
        int index=s->composing;
        size_t rows=s->tiles[index].h;
        HmiSurface surface = {s->buffers[index], s->buffer_pixels,
                              s->job.w, (uint16_t)rows, s->job.w};
        HmiResult rendered = s->compose(s->snapshot, s->tiles[index], &surface);
        if (rendered != HMI_OK && rendered != HMI_BUSY) return Fault(s, rendered);
        if(rendered==HMI_OK) {
            s->prepared=(int8_t)index;s->composing=-1;s->next_row+=(uint16_t)rows;
        }
    }
    if (s->active < 0 && s->prepared >= 0 && result == HMI_OK) {
        int index = s->prepared;
        result = s->transport.start(s->transport.context, s->tiles[index], s->buffers[index]);
        if (result == HMI_OK) { s->active = (int8_t)index; s->prepared = -1; }
        else if (result != HMI_BUSY) return Fault(s, result);
    }
    if (s->active < 0 && s->prepared < 0 && s->next_row == s->job.h) {
        s->busy = false; ++s->completed_jobs;
    }
    return s->busy ? HMI_BUSY : HMI_OK;
}
