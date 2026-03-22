#include "focus_camera_capture.h"

#include "focus_image_stream.h"
#include "home_config.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32

bool focus_camera_capture_start(void) { return false; }
void focus_camera_capture_set_paused(bool paused) { (void)paused; }
void focus_camera_capture_stop(void) {}

#else

#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#define CAPTURE_TMP_PATH "/tmp/focusframe_capture.jpg"
#define CAPTURE_MAX_BYTES (96 * 1024)

typedef struct {
    bool running;
    bool paused;
    pthread_t worker;
    uint32_t seq;
    pthread_mutex_t lock;
} CaptureState;

static CaptureState s_capture = {
    .running = false,
    .paused = false,
    .seq = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER,
};

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
}

static bool read_file(const char * path, uint8_t * out, size_t out_cap, size_t * out_len) {
    FILE * f = fopen(path, "rb");
    if (f == NULL) return false;

    size_t n = fread(out, 1, out_cap, f);
    fclose(f);

    if (n == 0) return false;
    *out_len = n;
    return true;
}

static bool capture_one_frame(void) {
    uint8_t jpeg[CAPTURE_MAX_BYTES];
    size_t jpeg_len = 0;

    char cmd[256];
    snprintf(
        cmd,
        sizeof(cmd),
        "libcamera-jpeg -n -t 1 --width 640 --height 480 -q 65 -o %s > /dev/null 2>&1",
        CAPTURE_TMP_PATH);

    int rc = system(cmd);
    if (rc != 0) {
        return false;
    }

    if (!read_file(CAPTURE_TMP_PATH, jpeg, sizeof(jpeg), &jpeg_len)) {
        return false;
    }

    pthread_mutex_lock(&s_capture.lock);
    uint32_t seq = s_capture.seq++;
    pthread_mutex_unlock(&s_capture.lock);

    return focus_image_stream_send_jpeg(jpeg, jpeg_len, now_ms(), seq);
}

static void * capture_worker(void * arg) {
    (void)arg;

    const unsigned int frame_interval_us = (unsigned int)(1000000U / HOME_GAZE_STREAM_FPS);

    while (1) {
        bool running = false;
        bool paused = false;

        pthread_mutex_lock(&s_capture.lock);
        running = s_capture.running;
        paused = s_capture.paused;
        pthread_mutex_unlock(&s_capture.lock);

        if (!running) break;

        if (paused) {
            usleep(50 * 1000);
            continue;
        }

        (void)capture_one_frame();
        usleep(frame_interval_us);
    }

    return NULL;
}

bool focus_camera_capture_start(void) {
    pthread_mutex_lock(&s_capture.lock);
    if (s_capture.running) {
        s_capture.paused = false;
        pthread_mutex_unlock(&s_capture.lock);
        return true;
    }

    s_capture.running = true;
    s_capture.paused = false;
    s_capture.seq = 0;
    pthread_mutex_unlock(&s_capture.lock);

    if (pthread_create(&s_capture.worker, NULL, capture_worker, NULL) != 0) {
        pthread_mutex_lock(&s_capture.lock);
        s_capture.running = false;
        pthread_mutex_unlock(&s_capture.lock);
        return false;
    }

    return true;
}

void focus_camera_capture_set_paused(bool paused) {
    pthread_mutex_lock(&s_capture.lock);
    s_capture.paused = paused;
    pthread_mutex_unlock(&s_capture.lock);
}

void focus_camera_capture_stop(void) {
    pthread_mutex_lock(&s_capture.lock);
    bool was_running = s_capture.running;
    s_capture.running = false;
    s_capture.paused = false;
    pthread_mutex_unlock(&s_capture.lock);

    if (was_running) {
        pthread_join(s_capture.worker, NULL);
    }
}

#endif
