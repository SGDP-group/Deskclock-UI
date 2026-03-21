/**
 * ring_buffer.c — Fixed-size circular buffer
 */

#include "ring_buffer.h"
#include <string.h>

void ring_push(RingBuffer * rb, float val) {
    rb->values[rb->head] = val;
    rb->head = (rb->head + 1) % RING_SIZE;
    if (rb->count < RING_SIZE) rb->count++;
}

float ring_get(RingBuffer * rb, int index) {
    int physical;
    if (rb->count < RING_SIZE) {
        /* Buffer not yet full — logical 0 is at physical 0 */
        physical = index;
    } else {
        /* Buffer full — logical 0 (oldest) is at rb->head */
        physical = (rb->head + index) % RING_SIZE;
    }
    return rb->values[physical];
}

void ring_clear(RingBuffer * rb) {
    memset(rb, 0, sizeof(RingBuffer));
}
