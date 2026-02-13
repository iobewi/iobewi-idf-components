#ifndef FREERTOS_RINGBUF_H
#define FREERTOS_RINGBUF_H

#include <stddef.h>

typedef void *RingbufHandle_t;

static inline void *xRingbufferReceive(RingbufHandle_t rb, size_t *item_size, int ticks_to_wait)
{
    (void)rb;
    (void)item_size;
    (void)ticks_to_wait;
    return NULL;
}

static inline void vRingbufferReturnItem(RingbufHandle_t rb, void *item)
{
    (void)rb;
    (void)item;
}

#endif
