#include "focus_image_stream.h"

#include "home_config.h"
#include "net_stream.h"

#include <stdio.h>
#include <string.h>

#define FOCUS_STREAM_MAGIC_0 'G'
#define FOCUS_STREAM_MAGIC_1 'Z'
#define FOCUS_STREAM_MAGIC_2 'T'
#define FOCUS_STREAM_MAGIC_3 'K'
#define FOCUS_STREAM_VERSION 1

#define FOCUS_STREAM_MAX_HEADER 256
#define FOCUS_STREAM_MAX_FRAME_PACKET (96 * 1024)

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

static bool write_u16_be(uint8_t * out, size_t cap, size_t * pos, uint16_t value) {
    if (out == NULL || pos == NULL || (*pos + 2U) > cap) return false;
    out[*pos + 0U] = (uint8_t)((value >> 8U) & 0xFFU);
    out[*pos + 1U] = (uint8_t)(value & 0xFFU);
    *pos += 2U;
    return true;
}

static bool write_u32_be(uint8_t * out, size_t cap, size_t * pos, uint32_t value) {
    if (out == NULL || pos == NULL || (*pos + 4U) > cap) return false;
    out[*pos + 0U] = (uint8_t)((value >> 24U) & 0xFFU);
    out[*pos + 1U] = (uint8_t)((value >> 16U) & 0xFFU);
    out[*pos + 2U] = (uint8_t)((value >> 8U) & 0xFFU);
    out[*pos + 3U] = (uint8_t)(value & 0xFFU);
    *pos += 4U;
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
    if (s_connected) return true;

    s_connected = net_stream_start(HOME_GAZE_STREAM_HOST, HOME_GAZE_STREAM_PORT);
    return s_connected;
}

bool focus_image_stream_start_task(int user_id, int subtask_id) {
    if (user_id <= 0 || subtask_id <= 0) return false;

    reset_state();

    s_stream_type = STREAM_TASK;
    s_user_id = user_id;
    s_subtask_id = subtask_id;

    return ensure_stream_connected();
}

bool focus_image_stream_start_quick(int user_id, const char * session_key) {
    if (user_id <= 0 || session_key == NULL || session_key[0] == '\0') return false;

    reset_state();

    s_stream_type = STREAM_QUICK;
    s_user_id = user_id;
    strncpy(s_session_key, session_key, sizeof(s_session_key) - 1U);
    s_session_key[sizeof(s_session_key) - 1U] = '\0';

    return ensure_stream_connected();
}

void focus_image_stream_set_paused(bool paused) {
    s_paused = paused;
}

void focus_image_stream_stop(void) {
    net_stream_stop();
    reset_state();
}

bool focus_image_stream_send_jpeg(const uint8_t * jpeg_data, size_t jpeg_len, uint64_t timestamp_ms, uint32_t seq) {
    char header_json[FOCUS_STREAM_MAX_HEADER];
    int header_len = 0;

    if (jpeg_data == NULL || jpeg_len == 0U) return false;
    if (s_stream_type == STREAM_NONE || s_paused) return false;
    if (jpeg_len > (size_t)(FOCUS_STREAM_MAX_FRAME_PACKET - 20)) return false;

    if (!ensure_stream_connected()) {
        return false;
    }

    if (s_stream_type == STREAM_TASK) {
        header_len = snprintf(
            header_json,
            sizeof(header_json),
            "{\"stream_type\":\"subtask\",\"user_id\":\"%d\",\"subtask_id\":%d,\"timestamp_ms\":%llu,\"seq\":%lu}",
            s_user_id,
            s_subtask_id,
            (unsigned long long)timestamp_ms,
            (unsigned long)seq);
    } else {
        header_len = snprintf(
            header_json,
            sizeof(header_json),
            "{\"stream_type\":\"session\",\"user_id\":\"%d\",\"session_key\":\"%s\",\"timestamp_ms\":%llu,\"seq\":%lu}",
            s_user_id,
            s_session_key,
            (unsigned long long)timestamp_ms,
            (unsigned long)seq);
    }

    if (header_len <= 0 || header_len >= (int)sizeof(header_json)) {
        return false;
    }

    size_t packet_len = 12U + (size_t)header_len + jpeg_len;
    if (packet_len > FOCUS_STREAM_MAX_FRAME_PACKET) {
        return false;
    }

    uint8_t packet[FOCUS_STREAM_MAX_FRAME_PACKET];
    size_t pos = 0U;

    packet[pos++] = FOCUS_STREAM_MAGIC_0;
    packet[pos++] = FOCUS_STREAM_MAGIC_1;
    packet[pos++] = FOCUS_STREAM_MAGIC_2;
    packet[pos++] = FOCUS_STREAM_MAGIC_3;

    if (!write_u16_be(packet, sizeof(packet), &pos, FOCUS_STREAM_VERSION)) return false;
    if (!write_u16_be(packet, sizeof(packet), &pos, (uint16_t)header_len)) return false;
    if (!write_u32_be(packet, sizeof(packet), &pos, (uint32_t)jpeg_len)) return false;

    memcpy(packet + pos, header_json, (size_t)header_len);
    pos += (size_t)header_len;

    memcpy(packet + pos, jpeg_data, jpeg_len);
    pos += jpeg_len;

    return net_stream_enqueue(packet, pos);
}
