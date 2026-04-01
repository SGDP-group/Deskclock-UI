#include "focus_image_stream.h"

#include "home_config.h"
#include "net_stream.h"

#include <stdio.h>
#include <string.h>

#define FOCUS_STREAM_MAGIC "GZTK"
#define FOCUS_STREAM_PROTOCOL_VERSION 1U
#define FOCUS_STREAM_FIXED_HEADER_BYTES 12U
#define FOCUS_STREAM_MAX_JSON_HEADER 256U

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
static uint32_t s_stream_starts = 0;
static uint32_t s_frames_enqueued = 0;
static uint32_t s_frames_rejected = 0;
static char s_last_error[96] = {0};
static uint8_t s_packet_buf[HOME_GAZE_STREAM_MAX_PACKET_BYTES];

static void set_last_error(const char * msg) {
    if (msg == NULL) {
        return;
    }
    strncpy(s_last_error, msg, sizeof(s_last_error) - 1U);
    s_last_error[sizeof(s_last_error) - 1U] = '\0';
}

static void reset_state(void) {
    s_stream_type = STREAM_NONE;
    s_user_id = 0;
    s_subtask_id = 0;
    s_session_key[0] = '\0';
    s_paused = false;
}

static void write_u16_be(uint8_t * out, uint16_t value) {
    out[0] = (uint8_t)((value >> 8) & 0xFFU);
    out[1] = (uint8_t)(value & 0xFFU);
}

static void write_u32_be(uint8_t * out, uint32_t value) {
    out[0] = (uint8_t)((value >> 24) & 0xFFU);
    out[1] = (uint8_t)((value >> 16) & 0xFFU);
    out[2] = (uint8_t)((value >> 8) & 0xFFU);
    out[3] = (uint8_t)(value & 0xFFU);
}

static bool is_safe_session_char(char c) {
    return ((c >= 'a' && c <= 'z')
         || (c >= 'A' && c <= 'Z')
         || (c >= '0' && c <= '9')
         || c == '-'
         || c == '_');
}

static void sanitize_session_key(char * out, size_t out_len, const char * in) {
    size_t w = 0U;
    if (out == NULL || out_len == 0U) {
        return;
    }

    if (in != NULL) {
        for (size_t i = 0U; in[i] != '\0' && w + 1U < out_len; i++) {
            out[w++] = is_safe_session_char(in[i]) ? in[i] : '_';
        }
    }

    if (w == 0U && out_len > 1U) {
        out[w++] = 's';
    }

    out[w] = '\0';
}

static bool start_transport(void) {
    if (!net_stream_start(HOME_GAZE_STREAM_HOST, HOME_GAZE_STREAM_PORT)) {
        set_last_error("stream start failed");
        return false;
    }

    set_last_error("ok");
    return true;
}

static size_t build_stream_header_json(char * out,
                                       size_t out_cap,
                                       uint64_t timestamp_ms) {
    int n = 0;

    if (out == NULL || out_cap == 0U) {
        return 0U;
    }

    if (s_stream_type == STREAM_TASK) {
        n = snprintf(out,
                     out_cap,
                     "{\"stream_type\":\"subtask\",\"user_id\":\"%d\",\"subtask_id\":%d,\"timestamp_ms\":%llu}",
                     s_user_id,
                     s_subtask_id,
                     (unsigned long long)timestamp_ms);
    } else if (s_stream_type == STREAM_QUICK) {
        n = snprintf(out,
                     out_cap,
                     "{\"stream_type\":\"session\",\"user_id\":\"%d\",\"session_key\":\"%s\",\"timestamp_ms\":%llu}",
                     s_user_id,
                     s_session_key,
                     (unsigned long long)timestamp_ms);
    }

    if (n <= 0 || (size_t)n >= out_cap) {
        return 0U;
    }

    return (size_t)n;
}

bool focus_image_stream_start_task(int user_id, int subtask_id) {
    if (user_id <= 0 || subtask_id <= 0) {
        s_frames_rejected++;
        set_last_error("invalid task stream args");
        return false;
    }

    net_stream_stop();
    reset_state();

    s_stream_type = STREAM_TASK;
    s_user_id = user_id;
    s_subtask_id = subtask_id;
    s_stream_starts++;

    if (!start_transport()) {
        s_frames_rejected++;
        reset_state();
        return false;
    }

    return true;
}

bool focus_image_stream_start_quick(int user_id, const char * session_key) {
    if (user_id <= 0 || session_key == NULL || session_key[0] == '\0') {
        s_frames_rejected++;
        set_last_error("invalid quick stream args");
        return false;
    }

    net_stream_stop();
    reset_state();

    s_stream_type = STREAM_QUICK;
    s_user_id = user_id;
    sanitize_session_key(s_session_key, sizeof(s_session_key), session_key);
    s_stream_starts++;

    if (!start_transport()) {
        s_frames_rejected++;
        reset_state();
        return false;
    }

    return true;
}

void focus_image_stream_set_paused(bool paused) {
    s_paused = paused;
}

void focus_image_stream_stop(void) {
    net_stream_stop();
    reset_state();
    set_last_error("stopped");
}

bool focus_image_stream_send_jpeg(const uint8_t * jpeg_data,
                                  size_t jpeg_len,
                                  uint32_t image_width,
                                  uint32_t image_height,
                                  uint64_t timestamp_ms,
                                  uint32_t seq) {
    (void)image_width;
    (void)image_height;
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

    char json_header[FOCUS_STREAM_MAX_JSON_HEADER];
    size_t json_header_len = build_stream_header_json(json_header, sizeof(json_header), timestamp_ms);
    if (json_header_len == 0U) {
        s_frames_rejected++;
        set_last_error("header json failed");
        return false;
    }

    size_t packet_len = FOCUS_STREAM_FIXED_HEADER_BYTES + json_header_len + jpeg_len;
    if (packet_len > HOME_GAZE_STREAM_MAX_PACKET_BYTES) {
        s_frames_rejected++;
        set_last_error("packet too large");
        return false;
    }

    memcpy(s_packet_buf, FOCUS_STREAM_MAGIC, 4U);
    write_u16_be(s_packet_buf + 4U, FOCUS_STREAM_PROTOCOL_VERSION);
    write_u16_be(s_packet_buf + 6U, (uint16_t)json_header_len);
    write_u32_be(s_packet_buf + 8U, (uint32_t)jpeg_len);
    memcpy(s_packet_buf + FOCUS_STREAM_FIXED_HEADER_BYTES, json_header, json_header_len);
    memcpy(s_packet_buf + FOCUS_STREAM_FIXED_HEADER_BYTES + json_header_len, jpeg_data, jpeg_len);

    if (!net_stream_enqueue(s_packet_buf, packet_len)) {
        s_frames_rejected++;
        set_last_error("enqueue failed");
        return false;
    }

    s_frames_enqueued++;
    set_last_error("ok");
    return true;
}

FocusImageStreamStats focus_image_stream_get_stats(void) {
    FocusImageStreamStats stats;
    memset(&stats, 0, sizeof(stats));

    stats.active = (s_stream_type != STREAM_NONE);
    stats.connected = stats.active ? net_stream_is_connected() : false;
    stats.paused = s_paused;
    stats.stream_starts = s_stream_starts;
    stats.frames_enqueued = s_frames_enqueued;
    stats.frames_rejected = s_frames_rejected;
    stats.queue_depth = stats.active ? net_stream_queue_depth() : 0U;
    strncpy(stats.last_error, s_last_error, sizeof(stats.last_error) - 1U);
    stats.last_error[sizeof(stats.last_error) - 1U] = '\0';

    return stats;
}
