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

FocusCameraCaptureStats focus_camera_capture_get_stats(void) {
    FocusCameraCaptureStats stats;
    memset(&stats, 0, sizeof(stats));
    strncpy(stats.last_error, "camera unsupported on windows build", sizeof(stats.last_error) - 1U);
    return stats;
}

#else

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>
#include <linux/videodev2.h>

#define CAMERA_DEVICE "/dev/video0"
#define CAPTURE_WIDTH 640
#define CAPTURE_HEIGHT 480
#define CAPTURE_BUFFER_COUNT 4
#define V4L2_TIMEOUT_SEC 2

typedef struct {
    void * start;
    size_t length;
} MmapBuffer;

typedef struct {
    bool running;
    bool paused;
    bool camera_ready;
    int fd;
    pthread_t worker;
    uint32_t seq;
    uint32_t frames_captured;
    uint32_t frames_sent;
    uint32_t capture_failures;
    uint32_t send_failures;
    char last_error[96];
    MmapBuffer buffers[CAPTURE_BUFFER_COUNT];
    uint32_t buffer_count;
    pthread_mutex_t lock;
} CaptureState;

static CaptureState s_capture = {
    .running = false,
    .paused = false,
    .camera_ready = false,
    .fd = -1,
    .seq = 0,
    .frames_captured = 0,
    .frames_sent = 0,
    .capture_failures = 0,
    .send_failures = 0,
    .buffer_count = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER,
};

static void set_capture_error(const char * msg) {
    if (msg == NULL) return;
    strncpy(s_capture.last_error, msg, sizeof(s_capture.last_error) - 1U);
    s_capture.last_error[sizeof(s_capture.last_error) - 1U] = '\0';
}

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
}

static bool camera_open(void) {
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_streamparm parm;
    struct v4l2_requestbuffers req;

    s_capture.fd = open(CAMERA_DEVICE, O_RDWR);
    if (s_capture.fd < 0) {
        set_capture_error("open /dev/video0 failed");
        return false;
    }

    memset(&cap, 0, sizeof(cap));
    if (ioctl(s_capture.fd, VIDIOC_QUERYCAP, &cap) < 0) {
        set_capture_error("VIDIOC_QUERYCAP failed");
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) || !(cap.capabilities & V4L2_CAP_STREAMING)) {
        set_capture_error("camera missing capture/streaming caps");
        return false;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = CAPTURE_WIDTH;
    fmt.fmt.pix.height = CAPTURE_HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(s_capture.fd, VIDIOC_S_FMT, &fmt) < 0) {
        set_capture_error("VIDIOC_S_FMT MJPEG failed");
        return false;
    }

    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = HOME_GAZE_STREAM_FPS;
    (void)ioctl(s_capture.fd, VIDIOC_S_PARM, &parm);

    memset(&req, 0, sizeof(req));
    req.count = CAPTURE_BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(s_capture.fd, VIDIOC_REQBUFS, &req) < 0 || req.count == 0) {
        set_capture_error("VIDIOC_REQBUFS failed");
        return false;
    }

    s_capture.buffer_count = req.count;
    set_capture_error("ok");
    return true;
}

static bool camera_map_and_queue(void) {
    for (uint32_t i = 0; i < s_capture.buffer_count; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(s_capture.fd, VIDIOC_QUERYBUF, &buf) < 0) {
            set_capture_error("VIDIOC_QUERYBUF failed");
            return false;
        }

        s_capture.buffers[i].length = buf.length;
        s_capture.buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, s_capture.fd, buf.m.offset);
        if (s_capture.buffers[i].start == MAP_FAILED) {
            s_capture.buffers[i].start = NULL;
            set_capture_error("mmap buffer failed");
            return false;
        }

        if (ioctl(s_capture.fd, VIDIOC_QBUF, &buf) < 0) {
            set_capture_error("VIDIOC_QBUF failed");
            return false;
        }
    }

    return true;
}

static bool camera_stream_on(void) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_capture.fd, VIDIOC_STREAMON, &type) < 0) {
        set_capture_error("VIDIOC_STREAMON failed");
        return false;
    }

    return true;
}

static void camera_stream_off(void) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (s_capture.fd >= 0) {
        (void)ioctl(s_capture.fd, VIDIOC_STREAMOFF, &type);
    }
}

static void camera_close(void) {
    for (uint32_t i = 0; i < s_capture.buffer_count; i++) {
        if (s_capture.buffers[i].start != NULL && s_capture.buffers[i].length > 0) {
            munmap(s_capture.buffers[i].start, s_capture.buffers[i].length);
            s_capture.buffers[i].start = NULL;
            s_capture.buffers[i].length = 0;
        }
    }
    s_capture.buffer_count = 0;

    if (s_capture.fd >= 0) {
        close(s_capture.fd);
        s_capture.fd = -1;
    }
}

static bool capture_and_send_frame(void) {
    struct v4l2_buffer buf;
    fd_set read_fds;
    struct timeval tv;

    FD_ZERO(&read_fds);
    FD_SET(s_capture.fd, &read_fds);
    tv.tv_sec = V4L2_TIMEOUT_SEC;
    tv.tv_usec = 0;

    int sel = select(s_capture.fd + 1, &read_fds, NULL, NULL, &tv);
    if (sel <= 0) {
        set_capture_error(sel == 0 ? "camera frame timeout" : "select failed");
        return false;
    }

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(s_capture.fd, VIDIOC_DQBUF, &buf) < 0) {
        if (errno != EAGAIN) {
            set_capture_error("VIDIOC_DQBUF failed");
        }
        return false;
    }

    bool sent = false;
    if (buf.index < s_capture.buffer_count && buf.bytesused > 0) {
        s_capture.frames_captured++;
        sent = focus_image_stream_send_jpeg((const uint8_t *)s_capture.buffers[buf.index].start,
                                            (size_t)buf.bytesused,
                                            now_ms(),
                                            s_capture.seq++);
        if (sent) {
            s_capture.frames_sent++;
            set_capture_error("ok");
        } else {
            s_capture.send_failures++;
            set_capture_error("stream send failed");
        }
    } else {
        s_capture.capture_failures++;
        set_capture_error("invalid camera buffer");
    }

    if (ioctl(s_capture.fd, VIDIOC_QBUF, &buf) < 0) {
        set_capture_error("VIDIOC_QBUF recycle failed");
        return false;
    }

    return sent;
}

static void * capture_worker(void * arg) {
    (void)arg;

    if (!camera_open() || !camera_map_and_queue() || !camera_stream_on()) {
        pthread_mutex_lock(&s_capture.lock);
        s_capture.capture_failures++;
        s_capture.camera_ready = false;
        s_capture.running = false;
        pthread_mutex_unlock(&s_capture.lock);
        camera_close();
        return NULL;
    }

    pthread_mutex_lock(&s_capture.lock);
    s_capture.camera_ready = true;
    pthread_mutex_unlock(&s_capture.lock);

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

        if (!capture_and_send_frame()) {
            usleep(20 * 1000);
        }
    }

    camera_stream_off();
    camera_close();

    pthread_mutex_lock(&s_capture.lock);
    s_capture.camera_ready = false;
    pthread_mutex_unlock(&s_capture.lock);

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
    s_capture.camera_ready = false;
    s_capture.seq = 0;
    s_capture.frames_captured = 0;
    s_capture.frames_sent = 0;
    s_capture.capture_failures = 0;
    s_capture.send_failures = 0;
    set_capture_error("starting camera");
    pthread_mutex_unlock(&s_capture.lock);

    if (pthread_create(&s_capture.worker, NULL, capture_worker, NULL) != 0) {
        pthread_mutex_lock(&s_capture.lock);
        s_capture.running = false;
        s_capture.camera_ready = false;
        s_capture.capture_failures++;
        set_capture_error("capture thread create failed");
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

FocusCameraCaptureStats focus_camera_capture_get_stats(void) {
    FocusCameraCaptureStats stats;
    memset(&stats, 0, sizeof(stats));

    pthread_mutex_lock(&s_capture.lock);
    stats.running = s_capture.running;
    stats.paused = s_capture.paused;
    stats.camera_ready = s_capture.camera_ready;
    stats.frames_captured = s_capture.frames_captured;
    stats.frames_sent = s_capture.frames_sent;
    stats.capture_failures = s_capture.capture_failures;
    stats.send_failures = s_capture.send_failures;
    strncpy(stats.last_error, s_capture.last_error, sizeof(stats.last_error) - 1U);
    stats.last_error[sizeof(stats.last_error) - 1U] = '\0';
    pthread_mutex_unlock(&s_capture.lock);

    return stats;
}

#endif
