#ifndef FOCUS_CAMERA_CAPTURE_H
#define FOCUS_CAMERA_CAPTURE_H

#include <stdbool.h>
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

/* Pauses/resumes frame capture worker. */
void focus_camera_capture_set_paused(bool paused);

/* Stops camera capture worker and releases resources. */
void focus_camera_capture_stop(void);

/* Gets capture runtime diagnostics for troubleshooting on-device. */
FocusCameraCaptureStats focus_camera_capture_get_stats(void);

#endif /* FOCUS_CAMERA_CAPTURE_H */
