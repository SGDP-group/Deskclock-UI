#ifndef FOCUS_IMAGE_STREAM_H
#define FOCUS_IMAGE_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Starts frame stream for a task-backed session. */
bool focus_image_stream_start_task(int user_id, int subtask_id);

/* Starts frame stream for a quick session. */
bool focus_image_stream_start_quick(int user_id, const char * session_key);

/* Pauses or resumes frame publishing. */
void focus_image_stream_set_paused(bool paused);

/* Stops frame stream and clears session metadata. */
void focus_image_stream_stop(void);

/* Encodes framing header and pushes one jpeg payload over socket queue. */
bool focus_image_stream_send_jpeg(const uint8_t * jpeg_data, size_t jpeg_len, uint64_t timestamp_ms, uint32_t seq);

#endif /* FOCUS_IMAGE_STREAM_H */
