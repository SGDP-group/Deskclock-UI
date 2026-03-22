#ifndef FOCUS_CAMERA_CAPTURE_H
#define FOCUS_CAMERA_CAPTURE_H

#include <stdbool.h>

/* Starts camera capture worker (Linux/Pi path). */
bool focus_camera_capture_start(void);

/* Pauses/resumes frame capture worker. */
void focus_camera_capture_set_paused(bool paused);

/* Stops camera capture worker and releases resources. */
void focus_camera_capture_stop(void);

#endif /* FOCUS_CAMERA_CAPTURE_H */
