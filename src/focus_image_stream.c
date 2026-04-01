#include "focus_image_stream.h"

#include "home_config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#define FOCUS_STREAM_HTTP_RESPONSE_MAX 1024
#define FOCUS_STREAM_MAX_BASE64 (((HOME_GAZE_STREAM_MAX_FRAME_BYTES + 2U) / 3U) * 4U + 1U)

typedef enum {
    STREAM_NONE = 0,
    STREAM_TASK,
    STREAM_QUICK,
} StreamType;

static StreamType s_stream_type = STREAM_NONE;
static int s_user_id = 0;
static int s_subtask_id = 0;
static char s_session_key[64] = {0};
static bool s_paused = false;
static bool s_connected = false;
static uint32_t s_stream_starts = 0;
static uint32_t s_frames_enqueued = 0;
static uint32_t s_frames_rejected = 0;
static char s_last_error[96] = {0};
static char s_frame_b64[FOCUS_STREAM_MAX_BASE64];
static char * s_json_body = NULL;
static size_t s_json_body_cap = 0;

static void set_last_error(const char * msg) {
    if (msg == NULL) return;
    strncpy(s_last_error, msg, sizeof(s_last_error) - 1U);
    s_last_error[sizeof(s_last_error) - 1U] = '\0';
}

static bool ensure_json_capacity(size_t required_len) {
    if ((required_len + 1U) <= s_json_body_cap) {
        return true;
    }

    char * resized = (char *)realloc(s_json_body, required_len + 1U);
    if (resized == NULL) {
        return false;
    }

    s_json_body = resized;
    s_json_body_cap = required_len + 1U;
    return true;
}

static size_t base64_encode(const uint8_t * in, size_t in_len, char * out, size_t out_cap) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    if (in == NULL || out == NULL) {
        return 0U;
    }

    size_t out_len = ((in_len + 2U) / 3U) * 4U;
    if (out_cap <= out_len) {
        return 0U;
    }

    size_t i = 0U;
    size_t j = 0U;
    while (i + 2U < in_len) {
        uint32_t tri = ((uint32_t)in[i] << 16U)
                     | ((uint32_t)in[i + 1U] << 8U)
                     | ((uint32_t)in[i + 2U]);
        out[j++] = table[(tri >> 18U) & 0x3FU];
        out[j++] = table[(tri >> 12U) & 0x3FU];
        out[j++] = table[(tri >> 6U) & 0x3FU];
        out[j++] = table[tri & 0x3FU];
        i += 3U;
    }

    if (i < in_len) {
        uint32_t tri = (uint32_t)in[i] << 16U;
        out[j++] = table[(tri >> 18U) & 0x3FU];

        if (i + 1U < in_len) {
            tri |= (uint32_t)in[i + 1U] << 8U;
            out[j++] = table[(tri >> 12U) & 0x3FU];
            out[j++] = table[(tri >> 6U) & 0x3FU];
            out[j++] = '=';
        } else {
            out[j++] = table[(tri >> 12U) & 0x3FU];
            out[j++] = '=';
            out[j++] = '=';
        }
    }

    out[out_len] = '\0';
    return out_len;
}

static void format_iso8601_utc(uint64_t timestamp_ms, char * out, size_t out_len) {
    if (out == NULL || out_len == 0U) {
        return;
    }

    time_t epoch_sec = (time_t)(timestamp_ms / 1000ULL);
    struct tm utc_tm;
    memset(&utc_tm, 0, sizeof(utc_tm));

#ifdef _WIN32
    gmtime_s(&utc_tm, &epoch_sec);
#else
    gmtime_r(&epoch_sec, &utc_tm);
#endif

    strftime(out, out_len, "%Y-%m-%dT%H:%M:%SZ", &utc_tm);
}

static bool send_all(int fd, const char * data, size_t len) {
    size_t sent = 0U;
    while (sent < len) {
#ifdef _WIN32
        int n = send(fd, data + sent, (int)(len - sent), 0);
#else
        ssize_t n = send(fd, data + sent, len - sent, 0);
#endif
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
#ifndef _WIN32
        if (n < 0 && errno == EINTR) {
            continue;
        }
#endif
        return false;
    }
    return true;
}

static bool response_success(const char * response) {
    if (response == NULL) {
        return false;
    }

    return (strncmp(response, "HTTP/1.1 2", 10) == 0)
        || (strncmp(response, "HTTP/1.0 2", 10) == 0);
}

static bool http_post_json(const char * path,
                           const char * body,
                           size_t body_len,
                           char * err,
                           size_t err_len) {
#ifdef _WIN32
    (void)path;
    (void)body;
    (void)body_len;
    if (err != NULL && err_len > 0U) {
        snprintf(err, err_len, "stream unsupported on windows build");
    }
    return false;
#else
    if (path == NULL || body == NULL || body_len == 0U) {
        if (err != NULL && err_len > 0U) {
            snprintf(err, err_len, "invalid request");
        }
        return false;
    }

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)HOME_GAZE_STREAM_PORT);

    struct addrinfo hints;
    struct addrinfo * res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(HOME_GAZE_STREAM_HOST, port_str, &hints, &res) != 0 || res == NULL) {
        if (err != NULL && err_len > 0U) {
            snprintf(err, err_len, "dns failed");
        }
        return false;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        if (err != NULL && err_len > 0U) {
            snprintf(err, err_len, "socket failed");
        }
        return false;
    }

    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        close(fd);
        freeaddrinfo(res);
        if (err != NULL && err_len > 0U) {
            snprintf(err, err_len, "connect failed");
        }
        return false;
    }
    freeaddrinfo(res);

    char header[512];
    int header_len = snprintf(header,
                              sizeof(header),
                              "POST %s HTTP/1.1\r\n"
                              "Host: %s\r\n"
                              "Connection: close\r\n"
                              "Content-Type: application/json\r\n"
                              "Accept: application/json\r\n"
                              "Content-Length: %lu\r\n\r\n",
                              path,
                              HOME_GAZE_STREAM_HOST,
                              (unsigned long)body_len);
    if (header_len <= 0 || (size_t)header_len >= sizeof(header)) {
        close(fd);
        if (err != NULL && err_len > 0U) {
            snprintf(err, err_len, "request too large");
        }
        return false;
    }

    if (!send_all(fd, header, (size_t)header_len) || !send_all(fd, body, body_len)) {
        close(fd);
        if (err != NULL && err_len > 0U) {
            snprintf(err, err_len, "send failed");
        }
        return false;
    }

    char response[FOCUS_STREAM_HTTP_RESPONSE_MAX];
    size_t used = 0U;
    while (used + 1U < sizeof(response)) {
        ssize_t n = recv(fd, response + used, sizeof(response) - used - 1U, 0);
        if (n <= 0) {
            break;
        }
        used += (size_t)n;
    }
    response[used] = '\0';
    close(fd);

    if (!response_success(response)) {
        if (err != NULL && err_len > 0U) {
            snprintf(err, err_len, "http error");
        }
        return false;
    }

    return true;
#endif
}

static bool start_focus_cloud_session(void) {
    if (s_stream_type == STREAM_NONE || s_user_id <= 0) {
        set_last_error("session args invalid");
        return false;
    }

    char body[256];
    if (s_stream_type == STREAM_TASK) {
        snprintf(body,
                 sizeof(body),
                 "{\"user_id\":\"%d\",\"session_name\":\"task_%d\"}",
                 s_user_id,
                 s_subtask_id);
    } else {
        snprintf(body,
                 sizeof(body),
                 "{\"user_id\":\"%d\",\"session_name\":\"%s\"}",
                 s_user_id,
                 s_session_key);
    }

    char err[96] = {0};
    if (!http_post_json(HOME_GAZE_STREAM_SESSION_START_PATH, body, strlen(body), err, sizeof(err))) {
        set_last_error(err[0] != '\0' ? err : "session start failed");
        s_connected = false;
        return false;
    }

    s_connected = true;
    set_last_error("ok");
    return true;
}

static void reset_state(void) {
    s_stream_type = STREAM_NONE;
    s_user_id = 0;
    s_subtask_id = 0;
    s_session_key[0] = '\0';
    s_paused = false;
    s_connected = false;
}

static bool ensure_stream_connected(void) {
    if (s_connected) {
        return true;
    }

    return start_focus_cloud_session();
}

bool focus_image_stream_start_task(int user_id, int subtask_id) {
    if (user_id <= 0 || subtask_id <= 0) {
        s_frames_rejected++;
        set_last_error("invalid task stream args");
        return false;
    }

    reset_state();

    s_stream_type = STREAM_TASK;
    s_user_id = user_id;
    s_subtask_id = subtask_id;
    s_stream_starts++;
    set_last_error("ok");

    return ensure_stream_connected();
}

bool focus_image_stream_start_quick(int user_id, const char * session_key) {
    if (user_id <= 0 || session_key == NULL || session_key[0] == '\0') {
        s_frames_rejected++;
        set_last_error("invalid quick stream args");
        return false;
    }

    reset_state();

    s_stream_type = STREAM_QUICK;
    s_user_id = user_id;
    strncpy(s_session_key, session_key, sizeof(s_session_key) - 1U);
    s_session_key[sizeof(s_session_key) - 1U] = '\0';
    s_stream_starts++;
    set_last_error("ok");

    return ensure_stream_connected();
}

void focus_image_stream_set_paused(bool paused) {
    s_paused = paused;
}

void focus_image_stream_stop(void) {
    s_connected = false;
    if (s_json_body != NULL) {
        free(s_json_body);
        s_json_body = NULL;
        s_json_body_cap = 0U;
    }
    reset_state();
}

bool focus_image_stream_send_jpeg(const uint8_t * jpeg_data,
                                  size_t jpeg_len,
                                  uint32_t image_width,
                                  uint32_t image_height,
                                  uint64_t timestamp_ms,
                                  uint32_t seq) {
    (void)seq;

    if (jpeg_data == NULL || jpeg_len == 0U) {
        s_frames_rejected++;
        set_last_error("empty jpeg payload");
        return false;
    }
    if (s_stream_type == STREAM_NONE) {
        s_frames_rejected++;
        set_last_error("stream not started");
        return false;
    }
    if (s_paused) {
        return false;
    }
    if (jpeg_len > HOME_GAZE_STREAM_MAX_FRAME_BYTES) {
        s_frames_rejected++;
        set_last_error("jpeg larger than packet");
        return false;
    }
    if (image_width == 0U || image_height == 0U) {
        s_frames_rejected++;
        set_last_error("invalid frame size");
        return false;
    }

    if (!ensure_stream_connected()) {
        s_frames_rejected++;
        return false;
    }

    size_t b64_len = base64_encode(jpeg_data, jpeg_len, s_frame_b64, sizeof(s_frame_b64));
    if (b64_len == 0U) {
        s_frames_rejected++;
        set_last_error("base64 encode failed");
        return false;
    }

    char ts_buf[32];
    format_iso8601_utc(timestamp_ms, ts_buf, sizeof(ts_buf));

    char json_prefix[96];
    int prefix_len = snprintf(json_prefix,
                              sizeof(json_prefix),
                              "{\"user_id\":\"%d\",\"frame_data\":\"",
                              s_user_id);
    if (prefix_len <= 0 || (size_t)prefix_len >= sizeof(json_prefix)) {
        s_frames_rejected++;
        set_last_error("json prefix failed");
        return false;
    }

    char json_suffix[160];
    int suffix_len = snprintf(json_suffix,
                              sizeof(json_suffix),
                              "\",\"image_width\":%lu,\"image_height\":%lu,\"timestamp\":\"%s\"}",
                              (unsigned long)image_width,
                              (unsigned long)image_height,
                              ts_buf);
    if (suffix_len <= 0 || (size_t)suffix_len >= sizeof(json_suffix)) {
        s_frames_rejected++;
        set_last_error("json suffix failed");
        return false;
    }

    size_t json_len = (size_t)prefix_len + b64_len + (size_t)suffix_len;
    if (!ensure_json_capacity(json_len)) {
        s_frames_rejected++;
        set_last_error("json alloc failed");
        return false;
    }

    memcpy(s_json_body, json_prefix, (size_t)prefix_len);
    memcpy(s_json_body + (size_t)prefix_len, s_frame_b64, b64_len);
    memcpy(s_json_body + (size_t)prefix_len + b64_len, json_suffix, (size_t)suffix_len);
    s_json_body[json_len] = '\0';

    char err[96] = {0};
    if (!http_post_json(HOME_GAZE_STREAM_ANALYZE_PATH, s_json_body, json_len, err, sizeof(err))) {
        s_connected = false;
        s_frames_rejected++;
        set_last_error(err[0] != '\0' ? err : "focus analyze failed");
        return false;
    }

    s_connected = true;
    s_frames_enqueued++;
    set_last_error("ok");
    return true;
}

FocusImageStreamStats focus_image_stream_get_stats(void) {
    FocusImageStreamStats stats;
    memset(&stats, 0, sizeof(stats));

    stats.active = (s_stream_type != STREAM_NONE);
    stats.connected = s_connected;
    stats.paused = s_paused;
    stats.stream_starts = s_stream_starts;
    stats.frames_enqueued = s_frames_enqueued;
    stats.frames_rejected = s_frames_rejected;
    stats.queue_depth = 0;
    strncpy(stats.last_error, s_last_error, sizeof(stats.last_error) - 1U);
    stats.last_error[sizeof(stats.last_error) - 1U] = '\0';

    return stats;
}
