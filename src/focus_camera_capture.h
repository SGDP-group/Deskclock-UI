#ifndef FOCUS_CAMERA_CAPTURE_H
#define FOCUS_CAMERA_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
	bool running;
	bool paused;
	bool camera_ready;
	uint32_t frames_captured;
	uint32_t frames_sent;
	uint32_t capture_failures;
	uint32_t send_failures;
	char last_error[96];
} FocusCameraCaptureStats;

/* Starts camera capture worker (Linux/Pi path). */
bool focus_camera_capture_start(void);

/* Starts local camera preview capture with network streaming disabled. */
bool focus_camera_capture_start_preview(void);

/* Enables/disables network frame streaming while keeping capture active. */
void focus_camera_capture_set_stream_enabled(bool enabled);

/* Pauses/resumes frame capture worker. */
void focus_camera_capture_set_paused(bool paused);

/* Stops camera capture worker and releases resources. */
void focus_camera_capture_stop(void);

/* Copies latest RGB565 preview frame; returns false if no frame is ready. */
bool focus_camera_capture_copy_latest_preview_rgb565(uint8_t * out_buf,
													 size_t out_cap,
													 uint32_t * out_w,
													 uint32_t * out_h,
													 uint32_t * out_seq);

/* Gets capture runtime diagnostics for troubleshooting on-device. */
FocusCameraCaptureStats focus_camera_capture_get_stats(void);

#endif /* FOCUS_CAMERA_CAPTURE_H */
