#include "screen_home.h"
#include "lvgl/lvgl.h"
#include "../styles/theme.h"
#include "../data/app_state.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <process.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <pthread.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#endif

#define HOME_API_HOST "127.0.0.1"
#define HOME_API_PORT 8080
#define HOME_API_PATH "/api/subtasks/due-today?userId=1"

#define HOME_HTTP_BUF_SIZE 16384
#define HOME_REFRESH_MS 180000
#define HOME_PENDING_POLL_MS 250

typedef struct {
    lv_obj_t * card;
    lv_obj_t * title;
    lv_obj_t * subtitle;
    lv_obj_t * time_range;
    lv_obj_t * status;
} TaskCardRefs;

typedef struct {
    bool ready;
    bool ok;
    uint8_t count;
    HomeTask tasks[APP_MAX_HOME_TASKS];
} PendingPayload;

static lv_obj_t * g_lbl_time = NULL;
static lv_obj_t * g_lbl_date = NULL;
static lv_obj_t * g_lbl_footer = NULL;
static lv_obj_t * g_task_list = NULL;
static TaskCardRefs g_cards[APP_MAX_HOME_TASKS];

static PendingPayload g_pending = {0};
static bool g_fetch_inflight = false;

#ifdef _WIN32
static CRITICAL_SECTION g_pending_cs;
static bool g_pending_cs_init = false;
static bool g_winsock_init = false;
#else
static pthread_mutex_t g_pending_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static void pending_lock(void) {
#ifdef _WIN32
    if (!g_pending_cs_init) {
        InitializeCriticalSection(&g_pending_cs);
        g_pending_cs_init = true;
    }
    EnterCriticalSection(&g_pending_cs);
#else
    pthread_mutex_lock(&g_pending_mutex);
#endif
}

static void pending_unlock(void) {
#ifdef _WIN32
    LeaveCriticalSection(&g_pending_cs);
#else
    pthread_mutex_unlock(&g_pending_mutex);
#endif
}

static void uppercase_ascii(char * text) {
    if (text == NULL) {
        return;
    }
    for (size_t i = 0; text[i] != '\0'; i++) {
        text[i] = (char)toupper((unsigned char)text[i]);
    }
}

static void update_clock_labels(void) {
    if (g_lbl_time == NULL || g_lbl_date == NULL) {
        return;
    }

    time_t now = time(NULL);
    struct tm local_tm;
#ifdef _WIN32
    localtime_s(&local_tm, &now);
#else
    localtime_r(&now, &local_tm);
#endif

    char time_buf[16] = {0};
    char date_buf[24] = {0};

    strftime(time_buf, sizeof(time_buf), "%H:%M", &local_tm);
    strftime(date_buf, sizeof(date_buf), "%a, %d %b", &local_tm);
    uppercase_ascii(date_buf);

    lv_label_set_text(g_lbl_time, time_buf);
    lv_label_set_text(g_lbl_date, date_buf);
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

static void format_time_range(const char * start, const char * end, char * out, size_t out_len) {
    if (out == NULL || out_len == 0) {
        return;
    }

    out[0] = '\0';

    if (start == NULL || end == NULL) {
        strncpy(out, "No time", out_len - 1);
        out[out_len - 1] = '\0';
        return;
    }

    const char * s = strchr(start, 'T');
    const char * e = strchr(end, 'T');
    if (s == NULL || e == NULL || strlen(s) < 6 || strlen(e) < 6) {
        strncpy(out, "No time", out_len - 1);
        out[out_len - 1] = '\0';
        return;
    }

    snprintf(out, out_len, "%.5s - %.5s", s + 1, e + 1);
}

static uint8_t parse_due_today_json(const char * body, HomeTask * out_tasks, uint8_t max_tasks) {
    if (body == NULL || out_tasks == NULL || max_tasks == 0) {
        return 0;
    }

    uint8_t count = 0;
    const char * p = body;

    while (*p != '\0' && count < max_tasks) {
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

        HomeTask * t = &out_tasks[count];
        memset(t, 0, sizeof(*t));

        json_get_int(obj_buf, "id", &t->id);
        json_get_bool(obj_buf, "completed", &t->completed);

        if (!json_get_string(obj_buf, "name", t->title, sizeof(t->title))) {
            json_get_string(obj_buf, "taskName", t->title, sizeof(t->title));
        }
        if (!json_get_string(obj_buf, "description", t->subtitle, sizeof(t->subtitle))) {
            strncpy(t->subtitle, "No description", sizeof(t->subtitle) - 1);
        }
        if (!json_get_string(obj_buf, "statusName", t->status, sizeof(t->status))) {
            strncpy(t->status, t->completed ? "DONE" : "PENDING", sizeof(t->status) - 1);
        }

        char start_time[40] = {0};
        char end_time[40] = {0};
        json_get_string(obj_buf, "startTime", start_time, sizeof(start_time));
        json_get_string(obj_buf, "endTime", end_time, sizeof(end_time));
        format_time_range(start_time, end_time, t->time_range, sizeof(t->time_range));

        if (t->title[0] == '\0') {
            strncpy(t->title, "Untitled task", sizeof(t->title) - 1);
        }

        count++;
        p = obj_end + 1;
    }

    return count;
}

static bool http_fetch_due_today(char * body_out, size_t body_out_len) {
    if (body_out == NULL || body_out_len == 0) {
        return false;
    }

    body_out[0] = '\0';

    char request[256];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n"
             "Accept: application/json\r\n\r\n",
             HOME_API_PATH, HOME_API_HOST);

    char response[HOME_HTTP_BUF_SIZE];
    size_t used = 0;
    response[0] = '\0';

#ifdef _WIN32
    if (!g_winsock_init) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            return false;
        }
        g_winsock_init = true;
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

    while (used + 1 < sizeof(response)) {
        int n = recv(sock, response + used, (int)(sizeof(response) - used - 1), 0);
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

    while (used + 1 < sizeof(response)) {
        ssize_t n = recv(sock, response + used, sizeof(response) - used - 1, 0);
        if (n <= 0) {
            break;
        }
        used += (size_t)n;
    }
    response[used] = '\0';
    close(sock);
#endif

    const char * body = strstr(response, "\r\n\r\n");
    if (body == NULL) {
        return false;
    }
    body += 4;

    strncpy(body_out, body, body_out_len - 1);
    body_out[body_out_len - 1] = '\0';
    return true;
}

static void render_loading_card(const char * title, const char * subtitle) {
    for (uint8_t i = 0; i < APP_MAX_HOME_TASKS; i++) {
        if (g_cards[i].card != NULL) {
            if (i == 0) {
                lv_label_set_text(g_cards[i].title, title);
                lv_label_set_text(g_cards[i].subtitle, subtitle);
                lv_label_set_text(g_cards[i].time_range, "");
                lv_label_set_text(g_cards[i].status, "");
                lv_obj_clear_flag(g_cards[i].card, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(g_cards[i].card, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

static void render_task_cards(void) {
    if (g_app_state.tasks_loading) {
        render_loading_card("Loading tasks", "Fetching from due-today endpoint");
        return;
    }

    if (g_app_state.home_task_count == 0) {
        render_loading_card("No tasks due", "You are clear for now");
        return;
    }

    for (uint8_t i = 0; i < APP_MAX_HOME_TASKS; i++) {
        if (g_cards[i].card == NULL) {
            continue;
        }

        if (i >= g_app_state.home_task_count) {
            lv_obj_add_flag(g_cards[i].card, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        const HomeTask * t = &g_app_state.home_tasks[i];
        lv_label_set_text(g_cards[i].title, t->title);
        lv_label_set_text(g_cards[i].subtitle, t->subtitle);
        lv_label_set_text(g_cards[i].time_range, t->time_range);
        lv_label_set_text(g_cards[i].status, t->status);

        lv_obj_set_style_text_color(g_cards[i].status,
                                    t->completed ? lv_color_hex(0x7BE495) : lv_color_hex(0xF9C74F),
                                    LV_PART_MAIN);

        lv_obj_clear_flag(g_cards[i].card, LV_OBJ_FLAG_HIDDEN);
    }
}

static void clock_timer_cb(lv_timer_t * timer) {
    (void)timer;
    update_clock_labels();
}

static void apply_pending_payload(void) {
    PendingPayload copy;
    memset(&copy, 0, sizeof(copy));

    pending_lock();
    if (!g_pending.ready) {
        pending_unlock();
        return;
    }
    copy = g_pending;
    g_pending.ready = false;
    pending_unlock();

    app_state_set_tasks_loading(false);
    if (copy.ok) {
        app_state_set_home_tasks(copy.tasks, copy.count);
        app_state_set_status("Due today updated");
    } else {
        app_state_set_home_tasks(NULL, 0);
        app_state_set_status("Failed to load tasks");
    }

    render_task_cards();
    if (g_lbl_footer != NULL) {
        lv_label_set_text(g_lbl_footer, g_app_state.status_message);
    }

    g_fetch_inflight = false;
}

static void pending_timer_cb(lv_timer_t * timer) {
    (void)timer;
    apply_pending_payload();
}

#ifdef _WIN32
static unsigned __stdcall fetch_due_today_thread(void * arg)
#else
static void * fetch_due_today_thread(void * arg)
#endif
{
    (void)arg;

    char body[HOME_HTTP_BUF_SIZE] = {0};
    HomeTask tasks[APP_MAX_HOME_TASKS];
    memset(tasks, 0, sizeof(tasks));

    bool ok = http_fetch_due_today(body, sizeof(body));
    uint8_t task_count = 0;
    if (ok) {
        task_count = parse_due_today_json(body, tasks, APP_MAX_HOME_TASKS);
    }

    pending_lock();
    g_pending.ok = ok;
    g_pending.count = task_count;
    memcpy(g_pending.tasks, tasks, sizeof(tasks));
    g_pending.ready = true;
    pending_unlock();

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static void start_due_today_fetch(void) {
    if (g_fetch_inflight) {
        return;
    }

    g_fetch_inflight = true;
    app_state_set_tasks_loading(true);
    render_task_cards();

#ifdef _WIN32
    uintptr_t thread_handle = _beginthreadex(NULL, 0, fetch_due_today_thread, NULL, 0, NULL);
    if (thread_handle == 0) {
        g_fetch_inflight = false;
        app_state_set_tasks_loading(false);
        app_state_set_status("Task loader unavailable");
        return;
    }
    CloseHandle((HANDLE)thread_handle);
#else
    pthread_t t;
    if (pthread_create(&t, NULL, fetch_due_today_thread, NULL) != 0) {
        g_fetch_inflight = false;
        app_state_set_tasks_loading(false);
        app_state_set_status("Task loader unavailable");
        return;
    }
    pthread_detach(t);
#endif
}

static void refresh_timer_cb(lv_timer_t * timer) {
    (void)timer;
    start_due_today_fetch();
}

static void quick_focus_event(lv_event_t * e) {
    (void)e;
    app_state_set_status("Quick Focus ready");
    if (g_lbl_footer != NULL) {
        lv_label_set_text(g_lbl_footer, g_app_state.status_message);
    }
}

static void create_task_cards(lv_obj_t * parent) {
    for (uint8_t i = 0; i < APP_MAX_HOME_TASKS; i++) {
        lv_obj_t * card = lv_obj_create(parent);
        apply_card_style(card);
        lv_obj_set_width(card, lv_pct(100));
        lv_obj_set_height(card, 116);
        lv_obj_set_style_pad_left(card, 14, LV_PART_MAIN);
        lv_obj_set_style_pad_right(card, 14, LV_PART_MAIN);
        lv_obj_set_style_pad_top(card, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(card, 10, LV_PART_MAIN);

        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t * text_col = lv_obj_create(card);
        lv_obj_remove_style_all(text_col);
        lv_obj_set_flex_grow(text_col, 1);
        lv_obj_set_height(text_col, lv_pct(100));
        lv_obj_set_layout(text_col, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(text_col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(text_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_gap(text_col, 4, LV_PART_MAIN);

        lv_obj_t * title = lv_label_create(text_col);
        lv_obj_set_width(title, lv_pct(100));
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
        lv_obj_set_style_text_color(title, lv_color_hex(0xF5F5F5), LV_PART_MAIN);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);

        lv_obj_t * subtitle = lv_label_create(text_col);
        lv_obj_set_width(subtitle, lv_pct(100));
        lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(subtitle, lv_color_hex(0xBDBDBD), LV_PART_MAIN);
        lv_label_set_long_mode(subtitle, LV_LABEL_LONG_DOT);

        lv_obj_t * time_range = lv_label_create(text_col);
        lv_obj_set_style_text_font(time_range, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(time_range, lv_color_hex(0x70D6FF), LV_PART_MAIN);

        lv_obj_t * right_col = lv_obj_create(card);
        lv_obj_remove_style_all(right_col);
        lv_obj_set_width(right_col, 84);
        lv_obj_set_height(right_col, lv_pct(100));
        lv_obj_set_layout(right_col, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(right_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(right_col, 6, LV_PART_MAIN);

        lv_obj_t * start_btn = lv_btn_create(right_col);
        apply_primary_btn_style(start_btn);
        lv_obj_set_size(start_btn, 78, 52);
        lv_obj_t * start_lbl = lv_label_create(start_btn);
        lv_label_set_text(start_lbl, "START");
        lv_obj_center(start_lbl);

        lv_obj_t * status = lv_label_create(right_col);
        lv_obj_set_style_text_font(status, &lv_font_montserrat_14, LV_PART_MAIN);

        g_cards[i].card = card;
        g_cards[i].title = title;
        g_cards[i].subtitle = subtitle;
        g_cards[i].time_range = time_range;
        g_cards[i].status = status;
    }
}

lv_obj_t * screen_home_create(void) {
    lv_obj_t * screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x08090D), LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 12, LV_PART_MAIN);
    lv_obj_set_layout(screen, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(screen, 10, LV_PART_MAIN);

    lv_obj_t * header = lv_obj_create(screen);
    lv_obj_remove_style_all(header);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_height(header, 124);
    lv_obj_set_layout(header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * left_col = lv_obj_create(header);
    lv_obj_remove_style_all(left_col);
    lv_obj_set_width(left_col, lv_pct(56));
    lv_obj_set_height(left_col, lv_pct(100));
    lv_obj_set_layout(left_col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(left_col, 2, LV_PART_MAIN);

    g_lbl_time = lv_label_create(left_col);
    lv_obj_set_style_text_font(g_lbl_time, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_lbl_time, lv_color_hex(0xF3F4F6), LV_PART_MAIN);

    g_lbl_date = lv_label_create(left_col);
    lv_obj_set_style_text_font(g_lbl_date, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_lbl_date, lv_color_hex(0xB8BDC7), LV_PART_MAIN);

    lv_obj_t * quick_btn = lv_btn_create(header);
    apply_primary_btn_style(quick_btn);
    lv_obj_set_width(quick_btn, lv_pct(40));
    lv_obj_set_height(quick_btn, 86);
    lv_obj_add_event_cb(quick_btn, quick_focus_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t * quick_lbl = lv_label_create(quick_btn);
    lv_label_set_text(quick_lbl, "Quick Focus");
    lv_obj_set_style_text_font(quick_lbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(quick_lbl);

    g_task_list = lv_obj_create(screen);
    lv_obj_set_width(g_task_list, lv_pct(100));
    lv_obj_set_flex_grow(g_task_list, 1);
    lv_obj_set_style_pad_all(g_task_list, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_task_list, lv_color_hex(0x0F1117), LV_PART_MAIN);
    lv_obj_set_style_border_color(g_task_list, lv_color_hex(0x1F2937), LV_PART_MAIN);
    lv_obj_set_style_border_width(g_task_list, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(g_task_list, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(g_task_list, 10, LV_PART_MAIN);
    lv_obj_set_scroll_dir(g_task_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_task_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_y(g_task_list, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_layout(g_task_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(g_task_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_task_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    create_task_cards(g_task_list);

    g_lbl_footer = lv_label_create(screen);
    lv_obj_set_style_text_font(g_lbl_footer, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_lbl_footer, lv_color_hex(0x8D99AE), LV_PART_MAIN);
    lv_label_set_text(g_lbl_footer, "Ready");

    g_lbl_status = g_lbl_footer;
    app_state_set_status("Ready");
    app_state_set_tasks_loading(true);

    update_clock_labels();
    render_task_cards();

    lv_timer_create(clock_timer_cb, 1000, NULL);
    lv_timer_create(refresh_timer_cb, HOME_REFRESH_MS, NULL);
    lv_timer_create(pending_timer_cb, HOME_PENDING_POLL_MS, NULL);

    start_due_today_fetch();

    return screen;
}
