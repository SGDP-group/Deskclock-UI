#include "focus_camera_capture.h"

#include "focus_image_stream.h"
#include "home_config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
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
#include <jpeglib.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>
#include <linux/videodev2.h>

#define CAMERA_DEVICE_COUNT 16
#define CAPTURE_WIDTH 640
#define CAPTURE_HEIGHT 480
#define CAPTURE_BUFFER_COUNT 4
#define V4L2_TIMEOUT_SEC 1
#define JPEG_QUALITY 70
#define JPEG_MAX_BYTES HOME_GAZE_STREAM_MAX_FRAME_BYTES

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
    uint32_t v4l2_pixfmt;
    uint32_t width;
    uint32_t height;
    char device_path[32];
    char last_error[96];
    MmapBuffer buffers[CAPTURE_BUFFER_COUNT];
    uint32_t buffer_count;
    uint64_t last_publish_ms;
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
    .v4l2_pixfmt = 0,
    .width = CAPTURE_WIDTH,
    .height = CAPTURE_HEIGHT,
    .buffer_count = 0,
    .last_publish_ms = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER,
};

static int s_probe_start = 0;

static const char * s_camera_candidates[CAMERA_DEVICE_COUNT] = {
    HOME_CAMERA_DEVICE,
    "/dev/video0",
    "/dev/video1",
    "/dev/video2",
    "/dev/video3",
    "/dev/video13",
    "/dev/video14",
    "/dev/video15",
    "/dev/video16",
    "/dev/video20",
    "/dev/video21",
    "/dev/video22",
    "/dev/video23",
    "/dev/video10",
    "/dev/video11",
    "/dev/video12",
};

static bool camera_map_and_queue(void);
static bool camera_stream_on(void);
static void camera_close(void);

static void set_capture_error(const char * msg) {
    if (msg == NULL) return;
    strncpy(s_capture.last_error, msg, sizeof(s_capture.last_error) - 1U);
    s_capture.last_error[sizeof(s_capture.last_error) - 1U] = '\0';
}

static void set_capture_errorf(const char * fmt, ...) {
    va_list args;

    if (fmt == NULL) return;
    va_start(args, fmt);
    vsnprintf(s_capture.last_error, sizeof(s_capture.last_error), fmt, args);
    va_end(args);
    s_capture.last_error[sizeof(s_capture.last_error) - 1U] = '\0';
}

static const char * pixfmt_name(uint32_t pixfmt) {
    if (pixfmt == V4L2_PIX_FMT_MJPEG) return "MJPEG";
    if (pixfmt == V4L2_PIX_FMT_YUYV) return "YUYV";
    return "UNKNOWN";
}

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
}

static uint8_t clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static void yuv_to_rgb(uint8_t y, int u, int v, uint8_t * r, uint8_t * g, uint8_t * b) {
    int c = (int)y - 16;
    int d = u - 128;
    int e = v - 128;

    int rr = (298 * c + 409 * e + 128) >> 8;
    int gg = (298 * c - 100 * d - 208 * e + 128) >> 8;
    int bb = (298 * c + 516 * d + 128) >> 8;

    *r = clamp_u8(rr);
    *g = clamp_u8(gg);
    *b = clamp_u8(bb);
}

static bool encode_yuyv_to_jpeg(const uint8_t * yuyv,
                                uint32_t width,
                                uint32_t height,
                                uint8_t * jpeg_out,
                                size_t jpeg_cap,
                                size_t * jpeg_len_out) {
    if (yuyv == NULL || jpeg_out == NULL || jpeg_len_out == NULL || width == 0 || height == 0) {
        return false;
    }

    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPROW row_pointer[1];
    unsigned char * mem = NULL;
    unsigned long mem_len = 0;

    uint8_t * rgb_row = (uint8_t *)malloc((size_t)width * 3U);
    if (rgb_row == NULL) {
        return false;
    }

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_mem_dest(&cinfo, &mem, &mem_len);

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, JPEG_QUALITY, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    while (cinfo.next_scanline < cinfo.image_height) {
        uint32_t y = cinfo.next_scanline;
        const uint8_t * src = yuyv + ((size_t)y * width * 2U);

        for (uint32_t x = 0; x < width; x += 2U) {
            uint8_t y0 = src[0];
            uint8_t u = src[1];
            uint8_t y1 = src[2];
            uint8_t v = src[3];

            yuv_to_rgb(y0, u, v, &rgb_row[(size_t)x * 3U + 0U], &rgb_row[(size_t)x * 3U + 1U], &rgb_row[(size_t)x * 3U + 2U]);
            if (x + 1U < width) {
                yuv_to_rgb(y1, u, v, &rgb_row[(size_t)(x + 1U) * 3U + 0U], &rgb_row[(size_t)(x + 1U) * 3U + 1U], &rgb_row[(size_t)(x + 1U) * 3U + 2U]);
            }

            src += 4;
        }

        row_pointer[0] = rgb_row;
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);

    bool ok = false;
    if (mem != NULL && mem_len > 0 && mem_len <= jpeg_cap) {
        memcpy(jpeg_out, mem, (size_t)mem_len);
        *jpeg_len_out = (size_t)mem_len;
        ok = true;
    }

    if (mem != NULL) {
        free(mem);
    }
    jpeg_destroy_compress(&cinfo);
    free(rgb_row);

    return ok;
}

static bool camera_supports_format_fd(int fd, uint32_t pixfmt) {
    struct v4l2_fmtdesc fmtdesc;
    memset(&fmtdesc, 0, sizeof(fmtdesc));
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        if (fmtdesc.pixelformat == pixfmt) {
            return true;
        }
        fmtdesc.index++;
    }

    return false;
}

static bool camera_open(void) {
    char last_reason[96] = "no camera device available";
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_streamparm parm;
    struct v4l2_requestbuffers req;

    s_capture.fd = -1;
    s_capture.buffer_count = 0;
    s_capture.device_path[0] = '\0';

    for (int n = 0; n < CAMERA_DEVICE_COUNT; n++) {
        int i = (s_probe_start + n) % CAMERA_DEVICE_COUNT;
        int fd = -1;
        uint32_t requested_pixfmt = 0;
        const char * candidate = s_camera_candidates[i];

        if (candidate == NULL || candidate[0] == '\0') {
            continue;
        }

        fd = open(candidate, O_RDWR);
        if (fd < 0) {
            continue;
        }

        memset(&cap, 0, sizeof(cap));
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
            snprintf(last_reason, sizeof(last_reason), "%s querycap failed", candidate);
            close(fd);
            continue;
        }

        if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) || !(cap.capabilities & V4L2_CAP_STREAMING)) {
            snprintf(last_reason, sizeof(last_reason), "%s lacks capture/streaming", candidate);
            close(fd);
            continue;
        }

        if (camera_supports_format_fd(fd, V4L2_PIX_FMT_MJPEG)) {
            requested_pixfmt = V4L2_PIX_FMT_MJPEG;
        } else if (camera_supports_format_fd(fd, V4L2_PIX_FMT_YUYV)) {
            requested_pixfmt = V4L2_PIX_FMT_YUYV;
        } else {
            snprintf(last_reason, sizeof(last_reason), "%s lacks MJPEG/YUYV", candidate);
            close(fd);
            continue;
        }

        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = CAPTURE_WIDTH;
        fmt.fmt.pix.height = CAPTURE_HEIGHT;
        fmt.fmt.pix.pixelformat = requested_pixfmt;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;

        if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
            snprintf(last_reason, sizeof(last_reason), "%s set fmt failed", candidate);
            close(fd);
            continue;
        }

        memset(&parm, 0, sizeof(parm));
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        parm.parm.capture.timeperframe.numerator = 1;
        parm.parm.capture.timeperframe.denominator = HOME_GAZE_STREAM_FPS;
        (void)ioctl(fd, VIDIOC_S_PARM, &parm);

        memset(&req, 0, sizeof(req));
        req.count = CAPTURE_BUFFER_COUNT;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;

        if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0 || req.count == 0 || req.count > CAPTURE_BUFFER_COUNT) {
            snprintf(last_reason, sizeof(last_reason), "%s reqbufs failed", candidate);
            close(fd);
            continue;
        }

        s_capture.fd = fd;
        s_capture.v4l2_pixfmt = fmt.fmt.pix.pixelformat;
        s_capture.width = fmt.fmt.pix.width;
        s_capture.height = fmt.fmt.pix.height;
        s_capture.buffer_count = req.count;
        s_probe_start = (i + 1) % CAMERA_DEVICE_COUNT;
        strncpy(s_capture.device_path, candidate, sizeof(s_capture.device_path) - 1U);
        s_capture.device_path[sizeof(s_capture.device_path) - 1U] = '\0';
        set_capture_errorf("device:%s fmt:%s", s_capture.device_path, pixfmt_name(s_capture.v4l2_pixfmt));
        return true;
    }

    set_capture_error(last_reason);
    return false;
}

static bool initialize_camera_pipeline(char * startup_error, size_t startup_error_len) {
    if (startup_error != NULL && startup_error_len > 0U) {
        startup_error[0] = '\0';
    }

    for (int attempt = 0; attempt < CAMERA_DEVICE_COUNT; attempt++) {
        if (!camera_open()) {
            if (startup_error != NULL && startup_error_len > 0U) {
                snprintf(startup_error, startup_error_len, "%s", s_capture.last_error);
            }
            break;
        }
        if (!camera_map_and_queue()) {
            if (startup_error != NULL && startup_error_len > 0U) {
                snprintf(startup_error, startup_error_len, "%s", s_capture.last_error);
            }
            camera_close();
            continue;
        }
        if (!camera_stream_on()) {
            if (startup_error != NULL && startup_error_len > 0U) {
                snprintf(startup_error, startup_error_len, "%s", s_capture.last_error);
            }
            camera_close();
            continue;
        }

        if (startup_error != NULL && startup_error_len > 0U) {
            startup_error[0] = '\0';
        }
        return true;
    }

    return false;
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
        set_capture_errorf("streamon failed on %s", s_capture.device_path[0] ? s_capture.device_path : "unknown device");
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

static bool capture_and_send_frame(uint8_t * jpeg_scratch, size_t jpeg_scratch_cap) {
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
        const uint64_t ts_ms = now_ms();
        const uint64_t min_publish_interval_ms = (HOME_GAZE_STREAM_FPS > 0)
                                               ? (1000ULL / (uint64_t)HOME_GAZE_STREAM_FPS)
                                               : 200ULL;
        const bool should_publish = (s_capture.last_publish_ms == 0ULL)
                                 || ((ts_ms - s_capture.last_publish_ms) >= min_publish_interval_ms);

        s_capture.frames_captured++;

        if (!should_publish) {
            sent = true;
        } else if (s_capture.v4l2_pixfmt == V4L2_PIX_FMT_MJPEG) {
            sent = focus_image_stream_send_jpeg((const uint8_t *)s_capture.buffers[buf.index].start,
                                                (size_t)buf.bytesused,
                                                ts_ms,
                                                s_capture.seq++);
        } else if (s_capture.v4l2_pixfmt == V4L2_PIX_FMT_YUYV) {
            size_t jpeg_len = 0;

            if (jpeg_scratch != NULL
             && encode_yuyv_to_jpeg((const uint8_t *)s_capture.buffers[buf.index].start,
                                    s_capture.width,
                                    s_capture.height,
                                    jpeg_scratch,
                                    jpeg_scratch_cap,
                                    &jpeg_len)) {
                sent = focus_image_stream_send_jpeg(jpeg_scratch, jpeg_len, ts_ms, s_capture.seq++);
            } else {
                s_capture.capture_failures++;
                set_capture_error("YUYV->JPEG encode failed");
                sent = false;
            }
        } else {
            s_capture.capture_failures++;
            set_capture_error("unsupported capture pixel format");
            sent = false;
        }

        if (sent) {
            if (should_publish) {
                s_capture.frames_sent++;
                s_capture.last_publish_ms = ts_ms;
            }
            set_capture_errorf("ok:%s %s", s_capture.device_path, pixfmt_name(s_capture.v4l2_pixfmt));
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

    char startup_error[96];
    uint8_t * jpeg_scratch = NULL;
    int consecutive_timeouts = 0;

    if (!initialize_camera_pipeline(startup_error, sizeof(startup_error))) {
        pthread_mutex_lock(&s_capture.lock);
        s_capture.capture_failures++;
        s_capture.camera_ready = false;
        s_capture.running = false;
        if (startup_error[0] != '\0') {
            set_capture_error(startup_error);
        }
        pthread_mutex_unlock(&s_capture.lock);
        camera_close();
        return NULL;
    }

    pthread_mutex_lock(&s_capture.lock);
    s_capture.camera_ready = true;
    pthread_mutex_unlock(&s_capture.lock);

    if (s_capture.v4l2_pixfmt == V4L2_PIX_FMT_YUYV) {
        jpeg_scratch = (uint8_t *)malloc(JPEG_MAX_BYTES);
        if (jpeg_scratch == NULL) {
            pthread_mutex_lock(&s_capture.lock);
            s_capture.capture_failures++;
            set_capture_error("jpeg scratch alloc failed");
            s_capture.running = false;
            pthread_mutex_unlock(&s_capture.lock);
        }
    }

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

        if (!capture_and_send_frame(jpeg_scratch, JPEG_MAX_BYTES)) {
            if (strncmp(s_capture.last_error, "camera frame timeout", 20U) == 0) {
                consecutive_timeouts++;
            } else {
                consecutive_timeouts = 0;
            }

            if (consecutive_timeouts >= 4) {
                consecutive_timeouts = 0;

                camera_stream_off();
                camera_close();

                pthread_mutex_lock(&s_capture.lock);
                s_capture.camera_ready = false;
                pthread_mutex_unlock(&s_capture.lock);

                if (initialize_camera_pipeline(startup_error, sizeof(startup_error))) {
                    pthread_mutex_lock(&s_capture.lock);
                    s_capture.camera_ready = true;
                    pthread_mutex_unlock(&s_capture.lock);
                } else {
                    pthread_mutex_lock(&s_capture.lock);
                    s_capture.capture_failures++;
                    set_capture_error(startup_error[0] != '\0' ? startup_error : "camera reinit failed");
                    pthread_mutex_unlock(&s_capture.lock);
                }
            }

            usleep(20 * 1000);
        } else {
            consecutive_timeouts = 0;
        }
    }

    if (jpeg_scratch != NULL) {
        free(jpeg_scratch);
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
    s_capture.last_publish_ms = 0;
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
