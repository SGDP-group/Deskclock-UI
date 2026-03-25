#include "home_api_client.h"
#include "home_config.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#endif

#define HOME_HTTP_BUF_SIZE 16384

static bool g_winsock_ready = false;

static void build_due_today_path(char * path, size_t path_len) {
    if (path == NULL || path_len == 0) {
        return;
    }

    time_t now = time(NULL);
    struct tm local_tm;
#ifdef _WIN32
    localtime_s(&local_tm, &now);
#else
    localtime_r(&now, &local_tm);
#endif

    char date_buf[16] = {0};
    char time_buf[16] = {0};
    strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &local_tm);
    strftime(time_buf, sizeof(time_buf), "%H%M%S", &local_tm);

    long tz_offset_minutes = 0;
#ifdef _WIN32
    TIME_ZONE_INFORMATION tzi;
    DWORD tz_id = GetTimeZoneInformation(&tzi);
    long bias = tzi.Bias;
    if (tz_id == TIME_ZONE_ID_DAYLIGHT) {
        bias += tzi.DaylightBias;
    } else if (tz_id == TIME_ZONE_ID_STANDARD) {
        bias += tzi.StandardBias;
    }
    tz_offset_minutes = -bias;
#else
    struct tm gmt_tm;
    gmtime_r(&now, &gmt_tm);
    time_t local_epoch = mktime(&local_tm);
    time_t gmt_as_local_epoch = mktime(&gmt_tm);
    tz_offset_minutes = (long)(difftime(local_epoch, gmt_as_local_epoch) / 60.0);
#endif

    snprintf(path, path_len,
             "/api/subtasks/due-today?userId=%d&deviceDate=%s&deviceTime=%s&deviceEpoch=%lld&tzOffsetMinutes=%ld",
             HOME_API_USER_ID,
             date_buf,
             time_buf,
             (long long)now,
             tz_offset_minutes);
}

static void copy_text_safe(char * dst, size_t dst_len, const char * src) {
    if (dst == NULL || dst_len == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

static bool json_get_string(const char * obj, const char * key, char * out, size_t out_len) {
    if (obj == NULL || key == NULL || out == NULL || out_len == 0) {
        return false;
    }

    char needle[48];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char * p = strstr(obj, needle);
    if (p == NULL) {
        return false;
    }

    p = strchr(p, ':');
    if (p == NULL) {
        return false;
    }
    p++;

    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
        p++;
    }

    if (*p != '"') {
        return false;
    }
    p++;

    size_t idx = 0;
    while (*p != '\0' && *p != '"' && idx + 1 < out_len) {
        if (*p == '\\' && *(p + 1) != '\0') {
            p++;
        }
        out[idx++] = *p++;
    }

    out[idx] = '\0';
    return idx > 0;
}

static bool json_get_bool(const char * obj, const char * key, bool * out) {
    if (obj == NULL || key == NULL || out == NULL) {
        return false;
    }

    char needle[48];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char * p = strstr(obj, needle);
    if (p == NULL) {
        return false;
    }

    p = strchr(p, ':');
    if (p == NULL) {
        return false;
    }
    p++;

    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
        p++;
    }

    if (strncmp(p, "true", 4) == 0) {
        *out = true;
        return true;
    }

    if (strncmp(p, "false", 5) == 0) {
        *out = false;
        return true;
    }

    return false;
}

static bool json_get_int(const char * obj, const char * key, int * out) {
    if (obj == NULL || key == NULL || out == NULL) {
        return false;
    }

    char needle[48];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char * p = strstr(obj, needle);
    if (p == NULL) {
        return false;
    }

    p = strchr(p, ':');
    if (p == NULL) {
        return false;
    }
    p++;

    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
        p++;
    }

    int value = 0;
    if (sscanf(p, "%d", &value) != 1) {
        return false;
    }

    *out = value;
    return true;
}

static bool http_send_request(const char * request, char * response, size_t response_len) {
    if (request == NULL || response == NULL || response_len == 0) {
        return false;
    }

    response[0] = '\0';
    size_t used = 0;

#ifdef _WIN32
    if (!g_winsock_ready) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            return false;
        }
        g_winsock_ready = true;
    }

    struct addrinfo hints;
    struct addrinfo * res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port[8];
    snprintf(port, sizeof(port), "%d", HOME_API_PORT);
    if (getaddrinfo(HOME_API_HOST, port, &hints, &res) != 0 || res == NULL) {
        return false;
    }

    SOCKET sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == INVALID_SOCKET) {
        freeaddrinfo(res);
        return false;
    }

    if (connect(sock, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
        closesocket(sock);
        freeaddrinfo(res);
        return false;
    }
    freeaddrinfo(res);

    send(sock, request, (int)strlen(request), 0);

    while (used + 1 < response_len) {
        int n = recv(sock, response + used, (int)(response_len - used - 1), 0);
        if (n <= 0) {
            break;
        }
        used += (size_t)n;
    }
    response[used] = '\0';
    closesocket(sock);
#else
    struct addrinfo hints;
    struct addrinfo * res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port[8];
    snprintf(port, sizeof(port), "%d", HOME_API_PORT);
    if (getaddrinfo(HOME_API_HOST, port, &hints, &res) != 0 || res == NULL) {
        return false;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        return false;
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        close(sock);
        freeaddrinfo(res);
        return false;
    }
    freeaddrinfo(res);

    send(sock, request, strlen(request), 0);

    while (used + 1 < response_len) {
        ssize_t n = recv(sock, response + used, response_len - used - 1, 0);
        if (n <= 0) {
            break;
        }
        used += (size_t)n;
    }
    response[used] = '\0';
    close(sock);
#endif

    return used > 0;
}

static void format_time_range(const char * start, const char * end, char * out, size_t out_len) {
    if (out == NULL || out_len == 0) {
        return;
    }

    out[0] = '\0';

    if (start == NULL || end == NULL) {
        copy_text_safe(out, out_len, "No time");
        return;
    }

    const char * s = strchr(start, 'T');
    const char * e = strchr(end, 'T');
    if (s == NULL || e == NULL || strlen(s) < 6 || strlen(e) < 6) {
        copy_text_safe(out, out_len, "No time");
        return;
    }

    snprintf(out, out_len, "%.5s - %.5s", s + 1, e + 1);
}

static uint8_t parse_due_today_json(const char * body, HomeApiTask * tasks, uint8_t cap) {
    if (body == NULL || tasks == NULL || cap == 0) {
        return 0;
    }

    uint8_t count = 0;
    const char * p = body;

    while (*p != '\0' && count < cap) {
        const char * obj_start = strchr(p, '{');
        if (obj_start == NULL) {
            break;
        }

        const char * obj_end = strchr(obj_start, '}');
        if (obj_end == NULL) {
            break;
        }

        size_t obj_len = (size_t)(obj_end - obj_start + 1);
        if (obj_len > 1023) {
            obj_len = 1023;
        }

        char obj_buf[1024];
        memcpy(obj_buf, obj_start, obj_len);
        obj_buf[obj_len] = '\0';

        HomeApiTask * t = &tasks[count];
        memset(t, 0, sizeof(*t));

        json_get_int(obj_buf, "id", &t->id);
        json_get_bool(obj_buf, "completed", &t->completed);

        if (!json_get_string(obj_buf, "name", t->title, sizeof(t->title))) {
            json_get_string(obj_buf, "taskName", t->title, sizeof(t->title));
        }

        if (!json_get_string(obj_buf, "description", t->subtitle, sizeof(t->subtitle))) {
            copy_text_safe(t->subtitle, sizeof(t->subtitle), "No description");
        }

        if (!json_get_string(obj_buf, "statusName", t->status, sizeof(t->status))) {
            copy_text_safe(t->status, sizeof(t->status), t->completed ? "DONE" : "PENDING");
        }

        char start_time[40] = {0};
        char end_time[40] = {0};
        json_get_string(obj_buf, "startTime", start_time, sizeof(start_time));
        json_get_string(obj_buf, "endTime", end_time, sizeof(end_time));
        format_time_range(start_time, end_time, t->time_range, sizeof(t->time_range));

        if (t->title[0] == '\0') {
            copy_text_safe(t->title, sizeof(t->title), "Untitled task");
        }

        count++;
        p = obj_end + 1;
    }

    return count;
}

static bool http_response_ok(const char * response) {
    /* Expect first line: "HTTP/1.x NNN ..."; accept any 2xx status. */
    const char * p = response;
    if (strncmp(p, "HTTP/", 5) != 0) {
        return false;
    }
    /* Skip "HTTP/1.x " */
    p = strchr(p, ' ');
    if (p == NULL) {
        return false;
    }
    p++;
    int status = 0;
    if (sscanf(p, "%d", &status) != 1) {
        return false;
    }
    if (status < 200 || status > 299) {
        fprintf(stderr, "[HTTP] Non-2xx status: %d\n", status);
        return false;
    }
    return true;
}

/*
 * Decode a chunked HTTP body in-place.
 * `buf` points to the first byte after the \r\n\r\n header separator.
 * Returns the length of the decoded data (always <= len).
 * The result is NUL-terminated.
 */
static size_t http_decode_chunked(char * buf, size_t len) {
    char * src = buf;
    char * dst = buf;
    char * end = buf + len;

    while (src < end) {
        /* Read chunk-size hex line */
        char * eol = strstr(src, "\r\n");
        if (eol == NULL) {
            break;
        }
        /* Parse hex size; ignore chunk extensions after ';' */
        unsigned long chunk_size = 0;
        if (sscanf(src, "%lx", &chunk_size) != 1) {
            break;
        }
        src = eol + 2; /* skip past CRLF after size line */
        if (chunk_size == 0) {
            break; /* last chunk */
        }
        if (src + chunk_size > (char *)end) {
            chunk_size = (unsigned long)(end - src); /* clamp to available */
        }
        memmove(dst, src, chunk_size);
        dst += chunk_size;
        src += chunk_size;
        /* skip trailing CRLF after chunk data */
        if (src + 2 <= end && src[0] == '\r' && src[1] == '\n') {
            src += 2;
        }
    }
    *dst = '\0';
    return (size_t)(dst - buf);
}

static bool http_fetch_due_today(char * body_out, size_t body_out_len) {
    if (body_out == NULL || body_out_len == 0) {
        return false;
    }

    body_out[0] = '\0';

    char path[256];
    build_due_today_path(path, sizeof(path));

    char request[512];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n"
             "Accept: application/json\r\n\r\n",
             path, HOME_API_HOST);

    char response[HOME_HTTP_BUF_SIZE];
    if (!http_send_request(request, response, sizeof(response))) {
        return false;
    }

    if (!http_response_ok(response)) {
        return false;
    }

    const char * body = strstr(response, "\r\n\r\n");
    if (body == NULL) {
        return false;
    }

    body += 4;
    size_t body_len = strlen(body);
    strncpy(body_out, body, body_out_len - 1);
    body_out[body_out_len - 1] = '\0';

    if (body_len > 0 && strstr(response, "Transfer-Encoding: chunked") != NULL) {
        http_decode_chunked(body_out, strlen(body_out));
    }

    return true;
}

static bool http_post_auth_token(char * body_out, size_t body_out_len) {
    if (body_out == NULL || body_out_len == 0) {
        return false;
    }

    char payload[192];
    snprintf(payload, sizeof(payload),
             "{\"ip\":\"%s\",\"callbackUrl\":\"%s\"}",
             HOME_DEVICE_IP,
             HOME_API_CALLBACK_URL);

    char request[384];
    snprintf(request, sizeof(request),
             "POST /authToken/generate HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n"
             "%s",
             HOME_API_HOST, HOME_API_PORT, strlen(payload), payload);

    char response[HOME_HTTP_BUF_SIZE];
    if (!http_send_request(request, response, sizeof(response))) {
        return false;
    }

    if (!http_response_ok(response)) {
        return false;
    }

    const char * body = strstr(response, "\r\n\r\n");
    if (body == NULL) {
        return false;
    }

    body += 4;
    size_t body_len = strlen(body);
    strncpy(body_out, body, body_out_len - 1);
    body_out[body_out_len - 1] = '\0';

    if (body_len > 0 && strstr(response, "Transfer-Encoding: chunked") != NULL) {
        http_decode_chunked(body_out, strlen(body_out));
    }

    return true;
}

bool home_api_fetch_due_today(HomeApiTask * tasks, uint8_t * out_count, uint8_t cap) {
    if (tasks == NULL || out_count == NULL || cap == 0) {
        return false;
    }

    *out_count = 0;
    memset(tasks, 0, sizeof(HomeApiTask) * cap);

    char body[HOME_HTTP_BUF_SIZE];
    if (!http_fetch_due_today(body, sizeof(body))) {
        return false;
    }

    *out_count = parse_due_today_json(body, tasks, cap);
    return true;
}

bool home_api_fetch_auth_token(char * token_out, size_t token_out_len) {
    if (token_out == NULL || token_out_len == 0) {
        return false;
    }

    token_out[0] = '\0';

    char body[HOME_HTTP_BUF_SIZE];
    if (!http_post_auth_token(body, sizeof(body))) {
        return false;
    }

    return json_get_string(body, "token", token_out, token_out_len);
}
