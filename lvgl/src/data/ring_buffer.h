#ifndef RING_BUFFER_H
#define RING_BUFFER_H

/**
 * ring_buffer.h — Fixed-size circular buffer for real-time data
 *
 * Use this for sensor history, chart data, or any stream of values
 * where you only care about the last N readings.
 *
 * It overwrites the oldest value when full — no malloc, no fragmentation.
 * Perfect for the Pi Zero W's constrained memory.
 */

#define RING_SIZE 120  /* Stores last 120 values — 2 minutes at 1 reading/sec */

typedef struct {
    float values[RING_SIZE];
    int   head;   /* Index of next write position */
    int   count;  /* How many values are currently stored (0 to RING_SIZE) */
} RingBuffer;

/* Push a new value — overwrites oldest when full */
void  ring_push(RingBuffer * rb, float val);

/* Read by logical index: 0 = oldest, count-1 = newest */
float ring_get(RingBuffer * rb, int index);

/* Reset the buffer */
void  ring_clear(RingBuffer * rb);

#endif /* RING_BUFFER_H */
