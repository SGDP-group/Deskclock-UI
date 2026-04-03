#ifndef HOME_CONFIG_H
#define HOME_CONFIG_H

#include "device_config.h"

/* Central home screen API config. */
#define HOME_API_HOST "16.16.217.234"
#define HOME_API_PORT 8080
#define HOME_API_USER_ID (device_config_get_user_id())

#define HOME_GAZE_STREAM_HOST "192.168.8.166"
#define HOME_GAZE_STREAM_PORT 8003
#define HOME_GAZE_STREAM_FPS 5
#define HOME_CAMERA_PREVIEW_FPS 5
#define HOME_CAMERA_DEVICE "/dev/video0"
#define HOME_GAZE_STREAM_MAX_FRAME_BYTES (256 * 1024)
#define HOME_GAZE_STREAM_MAX_PACKET_BYTES (HOME_GAZE_STREAM_MAX_FRAME_BYTES + 512)

#define HOME_GAZE_RTSP_FALLBACK_ENABLED 0
#define HOME_GAZE_RTSP_URL "rtsp://192.168.8.166:8554/deskclock"
#define HOME_GAZE_RTSP_FAIL_THRESHOLD 3

#define HOME_API_REFRESH_MS 180000
#define HOME_API_PENDING_POLL_MS 250

#define HOME_QUICK_SESSION_MINUTES 30
#define HOME_TASK_FALLBACK_MINUTES 30
#define HOME_FOCUS_CHUNK_MINUTES 30
#define HOME_BREAK_MINUTES 5
#define HOME_BONUS_FOCUS_MINUTES 5

/*
 * Countdown speed multiplier for local testing.
 * 1 = real-time countdown, 60 = countdown runs 60x faster (1 real second = 1 in-app minute).
 */
#define HOME_COUNTDOWN_SPEED_MULTIPLIER 60U

#endif /* HOME_CONFIG_H */
