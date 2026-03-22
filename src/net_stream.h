#ifndef NET_STREAM_H
#define NET_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Lightweight TCP streaming client tailored for Raspberry Pi Zero W.
 * Usage:
 *   net_stream_start("192.168.0.10", 5555);
 *   net_stream_enqueue(payload, payload_len);
 *   ... when finished ...
 *   net_stream_stop();
 */

bool net_stream_start(const char * host, uint16_t port);
void net_stream_stop(void);

/* Enqueue data to be sent by the background socket thread.
 * Returns false if the queue is full; data is copied internally.
 */
bool net_stream_enqueue(const void * data, size_t len);

/* Runtime diagnostics for stream transport health checks. */
bool net_stream_is_connected(void);
size_t net_stream_queue_depth(void);

#endif /* NET_STREAM_H */
