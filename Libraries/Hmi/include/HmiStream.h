#ifndef HMI_STREAM_H
#define HMI_STREAM_H
#include "HmiTypes.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Independent compositor/transport glue. No full framebuffer, heap, HAL or
 * IRQ work here. All functions are called from the same main/task context.
 * Driver poll returns BUSY until final wire bit leaves, OK when idle, or ERROR.
 * Driver start returning BUSY must retain no ownership. ERROR is latched;
 * reset/reinitialize hardware before reinitializing the stream. */
typedef struct {
    void *context;
    HmiResult (*start)(void *, HmiRect, const uint16_t *pixels);
    HmiResult (*poll)(void *);
} HmiTransport;
/* Compose an OPAQUE tile in screen coordinates; snapshot must remain immutable
 * until Busy becomes false. Drawing code converts screen coords to tile coords. */
typedef HmiResult (*HmiCompose)(void *snapshot, HmiRect tile, HmiSurface *surface);
typedef struct {
    HmiTransport transport;
    uint16_t *buffers[2];
    size_t buffer_pixels;
    uint16_t screen_width, screen_height;
    HmiRect job, tiles[2];
    HmiCompose compose;
    void *snapshot;
    uint16_t next_row;
    int8_t active, prepared, composing;
    bool busy;
    bool cancel_requested;
    HmiResult error;
    uint32_t completed_jobs, completed_tiles;
} HmiStream;
HmiResult HmiStream_Init(HmiStream *stream, HmiTransport transport,
                        uint16_t screen_width, uint16_t screen_height,
                        uint16_t *first, uint16_t *second, size_t buffer_pixels);
/* One immutable update at a time. Caller coalesces later value changes and
 * submits latest state after this job finishes. No unbounded queue/backlog. */
HmiResult HmiStream_Submit(HmiStream *stream, HmiRect dirty,
                          HmiCompose compose, void *snapshot);
/* Compose may return BUSY and resume on the SAME tile/buffer. No wire ownership
 * is granted until it returns OK. Cancellation drains a pending composition. */
HmiResult HmiStream_Process(HmiStream *stream);
/* Stop after the current hardware-owned tile drains. Never abort/reuse an
 * in-flight buffer; useful for immediate page changes without finishing a frame. */
void HmiStream_Cancel(HmiStream *stream);
#ifdef __cplusplus
}
#endif
#endif
