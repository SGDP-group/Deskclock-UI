#include "screen_home.h"
#include "lvgl/lvgl.h"
#include "../data/app_state.h"
#include "../components/session_confirm_popup.h"
#include "../ui.h"
#include "src/home_api_client.h"
#include "src/home_config.h"
#include "src/device_config.h"
#include "src/provisioning_service.h"
#include "src/doormount_service.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef _WIN32
#include <pthread.h>
#endif

/* -----------------------------------------------------------------------
 * Fonts
 * ----------------------------------------------------------------------- */
extern const lv_font_t lv_font_montserrat_48 ;

/* -----------------------------------------------------------------------
 * Design tokens — mirroring the React CSS variables
 *   --bg:               #090909
 *   --surface:          #1a1a1a
 *   --accent:           #09a672
 *   --accent-hover:     #13b982
 *   --text-primary:     #ffffff
 *   --text-secondary:   #b2b2b2
 *   --text-muted:       #555555
 *   --border:           rgba(255,255,255,0.08)
 * ----------------------------------------------------------------------- */
#define CLR_BG_TOP          0x181818
#define CLR_BG_BOTTOM       0x060606
#define CLR_SURFACE         0x1A1A1A
#define CLR_SURFACE_BTN_TOP 0x1D1D1D
#define CLR_SURFACE_BTN_BOT 0x151515
#define CLR_ACCENT          0x09A672
#define CLR_ACCENT_STRIP    0x10B981
#define CLR_TEXT_PRIMARY     0xFFFFFF
#define CLR_TEXT_CLOCK       0xF5F6F8
#define CLR_TEXT_DATE        0xF1F1F1
#define CLR_TEXT_SECONDARY   0xD7DBE0
#define CLR_TEXT_DESC        0xD2D2D2
#define CLR_TEXT_MUTED       0x848C99
#define CLR_BORDER_SUBTLE    0x3B4048
#define CLR_TIME_BADGE_BG    0xFFFFFF
#define CLR_TIME_BADGE_TEXT  0x111111
#define CLR_DOT_INACTIVE     0x6B6B6B
#define CLR_DOT_ACTIVE       0xFFFFFF

/* -----------------------------------------------------------------------
 * Layout constants — fixed 800x480 display
 * ----------------------------------------------------------------------- */
#define SCREEN_W           800
#define SCREEN_H           480

/* Header region */
#define HEADER_H           180
#define HEADER_PAD_TOP     16
#define HEADER_PAD_LEFT    24
#define HEADER_PAD_RIGHT   24

/* Clock */
#define CLOCK_LETTER_SPACE 26
#define DATE_FONT_SIZE     32   /* mapped to lv_font_montserrat_48 */

/* Quick Focus button */
#define QF_BTN_W           236
#define QF_BTN_H           120
#define QF_BTN_RADIUS      26

/* Settings button (between clock + Quick Focus) */
#define SETTINGS_BTN_SIZE  74
#define HEADER_CTRL_GAP    16

/* Settings/Doormount popup layout */
#define SETTINGS_POPUP_W   620
#define SETTINGS_POPUP_H   360
#define DOORMOUNT_POPUP_W  700
#define DOORMOUNT_POPUP_H  400
#define DOORMOUNT_SETUP_POLL_MS 150

/* Task carousel area */
#define TASK_AREA_PAD_X    24
#define TASK_CARD_H        210
#define TASK_CARD_GAP      18
#define TASK_CARD_RADIUS   32

/* Card internals */
#define CARD_ACCENT_W      16
#define CARD_BODY_PAD_L    20
#define CARD_BODY_PAD_T    24
#define CARD_START_BTN_W   190
#define CARD_START_BTN_R   32

/* Time badge inside card */
#define TIME_BADGE_H       36
#define TIME_BADGE_RADIUS  8
#define TIME_BADGE_PAD_X   12

/* Carousel indicator dots */
#define DOT_SIZE           8
#define DOT_ACTIVE_H       40
#define DOT_RIGHT_MARGIN   14
#define DOT_GAP            12

/* Footer */
#define FOOTER_H           28

/* Pull-to-refresh */
#define PULL_REFRESH_TRIGGER_PX        70
#define PULL_REFRESH_MAX_PULL_PX       120
#define PULL_REFRESH_COOLDOWN_MS       800
#define PULL_REFRESH_SPINNER_SIZE      34
#define PULL_REFRESH_SPINNER_X         (HEADER_PAD_LEFT + 188)
#define PULL_REFRESH_SPINNER_BASE_Y    (HEADER_PAD_TOP - 16)
#define PULL_REFRESH_SPINNER_MAX_Y     (HEADER_PAD_TOP + 46)

/* -----------------------------------------------------------------------
 * Task card pool
 * ----------------------------------------------------------------------- */
typedef struct {
    lv_obj_t * card;
    lv_obj_t * title;
    lv_obj_t * time_badge;
    lv_obj_t * subtitle;
    lv_obj_t * status;
} TaskCardRefs;

#define HOME_CARD_POOL_SIZE 6

static lv_obj_t * g_lbl_time     = NULL;
static lv_obj_t * g_lbl_date     = NULL;
static lv_obj_t * g_lbl_footer   = NULL;
static lv_obj_t * g_task_list    = NULL;
static lv_obj_t * g_pull_spinner = NULL;
static lv_obj_t * g_settings_overlay = NULL;
static lv_obj_t * g_reset_overlay = NULL;
static lv_obj_t * g_doormount_overlay = NULL;
static lv_obj_t * g_doormount_status = NULL;
static lv_obj_t * g_doormount_list = NULL;
static lv_timer_t * g_doormount_setup_timer = NULL;
static TaskCardRefs g_cards[HOME_CARD_POOL_SIZE];
static lv_obj_t * g_lbl_empty_state = NULL;
static DoormountNetworkList g_doormount_networks;
static volatile bool g_doormount_setup_inflight = false;
static volatile bool g_doormount_setup_done = false;
static bool g_doormount_setup_success = false;
static char g_doormount_setup_error[160] = {0};
static char g_doormount_selected_ssid[DOORMOUNT_SSID_MAX_LEN] = {0};

static bool g_fetch_inflight = false;
static bool g_pull_tracking = false;
static char g_last_time[16]  = {0};
static char g_last_date[24]  = {0};
static int32_t g_pull_start_y = 0;
static int32_t g_pull_delta_y = 0;
static uint32_t g_last_manual_refresh_tick = 0;
static lv_timer_t * g_clock_timer = NULL;
static lv_timer_t * g_refresh_timer = NULL;

/* -----------------------------------------------------------------------
 * Utility helpers
 * ----------------------------------------------------------------------- */
static void copy_text_safe(char * dst, size_t dst_len, const char * src) {
    if (dst == NULL || dst_len == 0) return;
    if (src == NULL) { dst[0] = '\0'; return; }
    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

static void uppercase_ascii(char * text) {
    if (text == NULL) return;
    for (size_t i = 0; text[i] != '\0'; i++) {
        text[i] = (char)toupper((unsigned char)text[i]);
    }
}

/* -----------------------------------------------------------------------
 * Clock logic
 * ----------------------------------------------------------------------- */
static void update_clock_labels(void) {
    if (g_lbl_time == NULL || g_lbl_date == NULL) return;

    time_t now = time(NULL);
    struct tm local_tm;
#ifdef _WIN32
    localtime_s(&local_tm, &now);
#else
    localtime_r(&now, &local_tm);
#endif

    char time_buf[16] = {0};
    char date_buf[24] = {0};

    strftime(time_buf, sizeof(time_buf), "%H : %M", &local_tm);
    strftime(date_buf, sizeof(date_buf), "%a, %d %b", &local_tm);
    uppercase_ascii(date_buf);

    if (strcmp(g_last_time, time_buf) != 0) {
        lv_label_set_text(g_lbl_time, time_buf);
        copy_text_safe(g_last_time, sizeof(g_last_time), time_buf);
    }
    if (strcmp(g_last_date, date_buf) != 0) {
        lv_label_set_text(g_lbl_date, date_buf);
        copy_text_safe(g_last_date, sizeof(g_last_date), date_buf);
    }
}

static void clock_timer_cb(lv_timer_t * timer) {
    (void)timer;
    update_clock_labels();
}

/* -----------------------------------------------------------------------
 * Task card rendering
 * ----------------------------------------------------------------------- */
static void render_empty_state(const char * text) {
    if (g_task_list != NULL) {
        lv_obj_add_flag(g_task_list, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_lbl_empty_state != NULL) {
        lv_label_set_text(g_lbl_empty_state, text);
        lv_obj_clear_flag(g_lbl_empty_state, LV_OBJ_FLAG_HIDDEN);
    }
}

static void render_task_cards(void) {
    if (g_app_state.tasks_loading) {
        render_empty_state("Loading tasks...");
        return;
    }
    if (g_app_state.home_task_count == 0) {
        render_empty_state("No tasks today, Enjoy your day :D");
        return;
    }
    
    /* Ensure list is visible and empty state text is hidden */
    if (g_task_list != NULL) {
        lv_obj_clear_flag(g_task_list, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_lbl_empty_state != NULL) {
        lv_obj_add_flag(g_lbl_empty_state, LV_OBJ_FLAG_HIDDEN);
    }

    uint8_t visible = g_app_state.home_task_count;
    if (visible > HOME_CARD_POOL_SIZE) visible = HOME_CARD_POOL_SIZE;

    for (uint8_t i = 0; i < HOME_CARD_POOL_SIZE; i++) {
        lv_obj_t * card = g_cards[i].card;
        if (card == NULL) continue;

        if (i >= visible) {
            lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        const HomeTask * task = &g_app_state.home_tasks[i];
        lv_label_set_text(g_cards[i].title,      task->title);
        lv_label_set_text(g_cards[i].subtitle,    task->subtitle);
        lv_label_set_text(g_cards[i].time_badge,  task->time_range);
        lv_label_set_text(g_cards[i].status,      task->status);

        lv_obj_set_style_text_color(
            g_cards[i].status,
            task->completed ? lv_color_hex(0x8EF2A5) : lv_color_hex(0xD8DEE9),
            LV_PART_MAIN);

        lv_obj_clear_flag(card, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh_footer_label(void) {
    if (g_lbl_footer != NULL) {
        lv_label_set_text(g_lbl_footer, g_app_state.status_message);
    }
}

static void pull_refresh_hide_spinner(void) {
    if (g_pull_spinner == NULL) return;

    lv_obj_set_y(g_pull_spinner, PULL_REFRESH_SPINNER_BASE_Y);
    lv_obj_set_style_opa(g_pull_spinner, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_opa(g_pull_spinner, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_add_flag(g_pull_spinner, LV_OBJ_FLAG_HIDDEN);
}

static void pull_refresh_update_spinner(int32_t drag_px) {
    if (g_pull_spinner == NULL) return;
    if (drag_px <= 0) {
        pull_refresh_hide_spinner();
        return;
    }

    int32_t clamped = drag_px;
    if (clamped > PULL_REFRESH_MAX_PULL_PX) {
        clamped = PULL_REFRESH_MAX_PULL_PX;
    }

    int32_t travel = PULL_REFRESH_SPINNER_MAX_Y - PULL_REFRESH_SPINNER_BASE_Y;
    int32_t y = PULL_REFRESH_SPINNER_BASE_Y + (clamped * travel) / PULL_REFRESH_TRIGGER_PX;
    if (y > PULL_REFRESH_SPINNER_MAX_Y) {
        y = PULL_REFRESH_SPINNER_MAX_Y;
    }

    uint8_t opa = (uint8_t)((clamped * LV_OPA_COVER) / PULL_REFRESH_TRIGGER_PX);
    if (opa > LV_OPA_COVER) {
        opa = LV_OPA_COVER;
    }

    lv_color_t indicator_color = (clamped >= PULL_REFRESH_TRIGGER_PX)
                                     ? lv_color_hex(CLR_ACCENT_STRIP)
                                     : lv_color_hex(CLR_TEXT_SECONDARY);

    lv_obj_set_y(g_pull_spinner, y);
    lv_obj_set_style_opa(g_pull_spinner, opa, LV_PART_MAIN);
    lv_obj_set_style_opa(g_pull_spinner, opa, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_pull_spinner, indicator_color, LV_PART_INDICATOR);
    lv_obj_clear_flag(g_pull_spinner, LV_OBJ_FLAG_HIDDEN);
}

/* -----------------------------------------------------------------------
 * API fetch
 * ----------------------------------------------------------------------- */
static void fetch_due_today_now(void) {
    HomeApiTask api_tasks[APP_MAX_HOME_TASKS];
    HomeTask    ui_tasks[APP_MAX_HOME_TASKS];
    memset(api_tasks, 0, sizeof(api_tasks));
    memset(ui_tasks,  0, sizeof(ui_tasks));

    uint8_t count = 0;
    bool ok = home_api_fetch_due_today(api_tasks, &count, HOME_CARD_POOL_SIZE);
    if (ok) {
        for (uint8_t i = 0; i < count; i++) {
            ui_tasks[i].id        = api_tasks[i].id;
            ui_tasks[i].completed = api_tasks[i].completed;
            ui_tasks[i].duration_minutes = api_tasks[i].duration_minutes;
            copy_text_safe(ui_tasks[i].title,      sizeof(ui_tasks[i].title),      api_tasks[i].title);
            copy_text_safe(ui_tasks[i].subtitle,   sizeof(ui_tasks[i].subtitle),   api_tasks[i].subtitle);
            copy_text_safe(ui_tasks[i].time_range, sizeof(ui_tasks[i].time_range), api_tasks[i].time_range);
            copy_text_safe(ui_tasks[i].status,     sizeof(ui_tasks[i].status),     api_tasks[i].status);
        }
    }

    app_state_set_tasks_loading(false);
    if (ok) {
        app_state_set_home_tasks(ui_tasks, count);
    } else {
        app_state_set_home_tasks(NULL, 0);
        app_state_set_status("Could not reach API");
    }

    render_task_cards();
    if (g_lbl_footer != NULL) {
        lv_label_set_text(g_lbl_footer, g_app_state.status_message);
    }
}

static void start_due_today_fetch(bool manual_trigger) {
    if (g_fetch_inflight) {
        if (manual_trigger) {
            app_state_set_status("Already updating...");
            refresh_footer_label();
            pull_refresh_hide_spinner();
        }
        return;
    }

    if (manual_trigger) {
        app_state_set_status("Refreshing...");
        refresh_footer_label();
        pull_refresh_update_spinner(PULL_REFRESH_TRIGGER_PX);
        lv_refr_now(NULL);
    }

    g_fetch_inflight = true;
    app_state_set_tasks_loading(true);
    render_task_cards();
    fetch_due_today_now();
    g_fetch_inflight = false;

    if (manual_trigger) {
        if (strcmp(g_app_state.status_message, "Refreshing...") == 0) {
            app_state_set_status("Ready");
            refresh_footer_label();
        }

        if (g_refresh_timer != NULL) {
            lv_timer_reset(g_refresh_timer);
            lv_timer_resume(g_refresh_timer);
        }

        pull_refresh_hide_spinner();
    }
}

static void refresh_timer_cb(lv_timer_t * timer) {
    (void)timer;
    start_due_today_fetch(false);
}

static void time_pull_refresh_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t * indev = lv_event_get_indev(e);
    lv_point_t point = {0, 0};

    if (indev != NULL) {
        lv_indev_get_point(indev, &point);
    }

    if (code == LV_EVENT_PRESSED) {
        g_pull_tracking = true;
        g_pull_start_y = point.y;
        g_pull_delta_y = 0;
        pull_refresh_hide_spinner();
        return;
    }

    if (code == LV_EVENT_PRESSING) {
        if (!g_pull_tracking) return;

        g_pull_delta_y = point.y - g_pull_start_y;
        if (g_pull_delta_y < 0) {
            g_pull_delta_y = 0;
        }
        pull_refresh_update_spinner(g_pull_delta_y);
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (!g_pull_tracking) return;

        g_pull_tracking = false;
        bool threshold_hit = g_pull_delta_y >= PULL_REFRESH_TRIGGER_PX;
        bool cooldown_done = lv_tick_elaps(g_last_manual_refresh_tick) >= PULL_REFRESH_COOLDOWN_MS;

        if (threshold_hit && cooldown_done) {
            g_last_manual_refresh_tick = lv_tick_get();
            start_due_today_fetch(true);
        } else {
            pull_refresh_hide_spinner();
        }

        g_pull_delta_y = 0;
    }
}

static void task_list_scroll_cb(lv_event_t * e) {
    lv_obj_t * list = lv_event_get_target(e);
    if (g_lbl_footer == NULL) return;
    
    /* Get scroll position. Check if scrolled down at all */
    int32_t scroll_y = lv_obj_get_scroll_y(list);
    
    /* Fade footer from full opacity down as we scroll */
    /* We'll fully hide the footer after scrolling 50 pixels or so */
    int32_t fade_distance = 50;
    uint8_t opacity = LV_OPA_COVER;
    
    if (scroll_y > 0) {
        /* Calculate fade: full opacity at 0, transparent at fade_distance+ */
        opacity = (uint8_t)((LV_OPA_COVER * (fade_distance - scroll_y)) / fade_distance);
        if (opacity < 0) opacity = 0;
    }
    
    lv_obj_set_style_text_opa(g_lbl_footer, opacity, LV_PART_MAIN);
}

static void start_session_from_popup(SessionConfirmKind kind, uint8_t task_index) {
    if (kind == SESSION_CONFIRM_KIND_QUICK) {
        ui_navigate_camera_preview("Quick Session", (uint32_t)HOME_QUICK_SESSION_MINUTES * 60U, true, -1);
        return;
    }

    if (task_index >= g_app_state.home_task_count) return;

    const HomeTask * task = &g_app_state.home_tasks[task_index];
    const char * task_title = (task->subtitle[0] != '\0') ? task->subtitle : task->title;
    uint32_t minutes = (task->duration_minutes > 0) ? (uint32_t)task->duration_minutes : (uint32_t)HOME_TASK_FALLBACK_MINUTES;

    ui_navigate_camera_preview(task_title, minutes * 60U, false, task->id);
}

static void quick_focus_event(lv_event_t * e) {
    (void)e;
    app_state_set_status("Quick Focus ready");
    session_confirm_popup_show_quick();
    if (g_lbl_footer != NULL) {
        lv_label_set_text(g_lbl_footer, g_app_state.status_message);
    }
}

static void close_overlay(lv_obj_t ** overlay, lv_obj_t ** status_label, lv_obj_t ** list) {
    if (overlay != NULL && *overlay != NULL) {
        lv_obj_del(*overlay);
        *overlay = NULL;
    }

    if (status_label != NULL) {
        *status_label = NULL;
    }

    if (list != NULL) {
        *list = NULL;
    }
}

static void settings_popup_close(void) {
    close_overlay(&g_settings_overlay, NULL, NULL);
}

static void reset_popup_close(void) {
    close_overlay(&g_reset_overlay, NULL, NULL);
}

static void doormount_popup_close(void) {
    close_overlay(&g_doormount_overlay, &g_doormount_status, &g_doormount_list);
    memset(&g_doormount_networks, 0, sizeof(g_doormount_networks));
}

static void doormount_scan_and_render(void);
static void doormount_setup_poll_cb(lv_timer_t * timer);

#ifndef _WIN32
static void * doormount_setup_worker(void * arg) {
    (void)arg;

    char error[160] = {0};
    bool ok = doormount_service_setup_selected(g_doormount_selected_ssid, error, sizeof(error));

    g_doormount_setup_success = ok;
    if (!ok) {
        copy_text_safe(g_doormount_setup_error,
                       sizeof(g_doormount_setup_error),
                       (error[0] != '\0') ? error : "Unknown error");
    } else {
        g_doormount_setup_error[0] = '\0';
    }

    g_doormount_setup_done = true;
    return NULL;
}
#endif

static void doormount_setup_poll_cb(lv_timer_t * timer) {
    (void)timer;

    if (!g_doormount_setup_inflight || !g_doormount_setup_done) {
        return;
    }

    g_doormount_setup_inflight = false;
    g_doormount_setup_done = false;

    if (g_doormount_setup_timer != NULL) {
        lv_timer_delete(g_doormount_setup_timer);
        g_doormount_setup_timer = NULL;
    }

    if (!g_doormount_setup_success) {
        if (g_doormount_list != NULL && lv_obj_is_valid(g_doormount_list)) {
            lv_obj_clear_state(g_doormount_list, LV_STATE_DISABLED);
        }

        if (g_doormount_status != NULL && lv_obj_is_valid(g_doormount_status)) {
            char status_line[196];
            snprintf(status_line,
                     sizeof(status_line),
                     "Setup failed: %s",
                     (g_doormount_setup_error[0] != '\0') ? g_doormount_setup_error : "Unknown error");
            lv_label_set_text(g_doormount_status, status_line);
        }

        app_state_set_status("Doormount setup failed");
        refresh_footer_label();
        return;
    }

    app_state_set_status("Doormount setup complete");
    refresh_footer_label();

    if (g_doormount_overlay != NULL && lv_obj_is_valid(g_doormount_overlay)) {
        doormount_popup_close();
    }
}

static void doormount_select_event(lv_event_t * e) {
    if (g_doormount_setup_inflight) {
        if (g_doormount_status != NULL && lv_obj_is_valid(g_doormount_status)) {
            lv_label_set_text(g_doormount_status, "Setup already in progress...");
        }
        return;
    }

    if (g_doormount_status == NULL || g_doormount_list == NULL) {
        return;
    }

    uint32_t idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    if (idx >= g_doormount_networks.count) {
        return;
    }

    const char * ssid = g_doormount_networks.ssids[idx];
    copy_text_safe(g_doormount_selected_ssid, sizeof(g_doormount_selected_ssid), ssid);

    char status_line[128];
    snprintf(status_line, sizeof(status_line), "Connecting to %s...", ssid);
    lv_label_set_text(g_doormount_status, status_line);
    lv_obj_add_state(g_doormount_list, LV_STATE_DISABLED);
    lv_refr_now(NULL);

    g_doormount_setup_inflight = true;
    g_doormount_setup_done = false;
    g_doormount_setup_success = false;
    g_doormount_setup_error[0] = '\0';

    if (g_doormount_setup_timer != NULL) {
        lv_timer_delete(g_doormount_setup_timer);
        g_doormount_setup_timer = NULL;
    }
    g_doormount_setup_timer = lv_timer_create(doormount_setup_poll_cb, DOORMOUNT_SETUP_POLL_MS, NULL);

#ifdef _WIN32
    char error[160] = {0};
    bool ok = doormount_service_setup_selected(g_doormount_selected_ssid, error, sizeof(error));
    g_doormount_setup_success = ok;
    if (!ok) {
        copy_text_safe(g_doormount_setup_error,
                       sizeof(g_doormount_setup_error),
                       (error[0] != '\0') ? error : "Unknown error");
    }
    g_doormount_setup_done = true;
#else
    pthread_t worker;
    if (pthread_create(&worker, NULL, doormount_setup_worker, NULL) != 0) {
        g_doormount_setup_inflight = false;
        if (g_doormount_setup_timer != NULL) {
            lv_timer_delete(g_doormount_setup_timer);
            g_doormount_setup_timer = NULL;
        }

        lv_obj_clear_state(g_doormount_list, LV_STATE_DISABLED);
        lv_label_set_text(g_doormount_status, "Failed to start setup worker.");
        app_state_set_status("Doormount setup failed");
        refresh_footer_label();
        return;
    }

    pthread_detach(worker);
#endif
}

static void doormount_scan_and_render(void) {
    if (g_doormount_status == NULL || g_doormount_list == NULL) {
        return;
    }

    if (g_doormount_setup_inflight) {
        lv_label_set_text(g_doormount_status, "Setup in progress... please wait.");
        return;
    }

    lv_obj_clean(g_doormount_list);
    memset(&g_doormount_networks, 0, sizeof(g_doormount_networks));

    if (!device_config_has_wifi_credentials() || device_config_get_user_id() <= 0) {
        lv_label_set_text(g_doormount_status, "Saved home Wi-Fi/userId is missing.");
        return;
    }

    lv_label_set_text(g_doormount_status, "Scanning DoorMount devices...");
    lv_refr_now(NULL);

    char error[128] = {0};
    bool ok = doormount_service_scan(&g_doormount_networks, error, sizeof(error));
    if (!ok) {
        char status_line[160];
        snprintf(status_line,
                 sizeof(status_line),
                 "Scan failed: %s",
                 (error[0] != '\0') ? error : "No devices found");
        lv_label_set_text(g_doormount_status, status_line);
        return;
    }

    for (uint8_t i = 0; i < g_doormount_networks.count; i++) {
        lv_obj_t * btn = lv_list_add_btn(g_doormount_list, LV_SYMBOL_WIFI, g_doormount_networks.ssids[i]);
        lv_obj_add_event_cb(btn, doormount_select_event, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }

    char status_line[96];
    snprintf(status_line,
             sizeof(status_line),
             "Found %u device(s). Select one to set up.",
             g_doormount_networks.count);
    lv_label_set_text(g_doormount_status, status_line);
}

static void doormount_rescan_event(lv_event_t * e) {
    (void)e;

    if (g_doormount_setup_inflight) {
        if (g_doormount_status != NULL && lv_obj_is_valid(g_doormount_status)) {
            lv_label_set_text(g_doormount_status, "Setup in progress... wait for completion.");
        }
        return;
    }

    doormount_scan_and_render();
}

static void doormount_close_event(lv_event_t * e) {
    (void)e;
    doormount_popup_close();
}

static void show_doormount_popup(void) {
    doormount_popup_close();

    lv_obj_t * screen = lv_scr_act();
    if (screen == NULL) {
        return;
    }

    g_doormount_overlay = lv_obj_create(screen);
    lv_obj_remove_style_all(g_doormount_overlay);
    lv_obj_set_size(g_doormount_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(g_doormount_overlay, 0, 0);
    lv_obj_set_style_bg_color(g_doormount_overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_doormount_overlay, LV_OPA_70, LV_PART_MAIN);
    lv_obj_clear_flag(g_doormount_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * panel = lv_obj_create(g_doormount_overlay);
    lv_obj_set_size(panel, DOORMOUNT_POPUP_W, DOORMOUNT_POPUP_H);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, lv_color_hex(CLR_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_hex(CLR_BORDER_SUBTLE), LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(panel, true, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * title = lv_label_create(panel);
    lv_label_set_text(title, "Setup Doormount");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    g_doormount_status = lv_label_create(panel);
    lv_obj_set_width(g_doormount_status, DOORMOUNT_POPUP_W - 80);
    lv_obj_set_style_text_font(g_doormount_status, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_doormount_status, lv_color_hex(CLR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_set_style_text_align(g_doormount_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(g_doormount_status, LV_LABEL_LONG_WRAP);
    lv_obj_align(g_doormount_status, LV_ALIGN_TOP_MID, 0, 88);
    lv_label_set_text(g_doormount_status, "Scanning DoorMount devices...");

    g_doormount_list = lv_list_create(panel);
    lv_obj_set_size(g_doormount_list, DOORMOUNT_POPUP_W - 70, 180);
    lv_obj_align(g_doormount_list, LV_ALIGN_TOP_MID, 0, 145);
    lv_obj_set_style_bg_color(g_doormount_list, lv_color_hex(CLR_SURFACE_BTN_BOT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_doormount_list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_doormount_list, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_doormount_list, lv_color_hex(CLR_BORDER_SUBTLE), LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_doormount_list, 8, LV_PART_MAIN);

    lv_obj_t * actions = lv_obj_create(panel);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, lv_pct(100), 60);
    lv_obj_align(actions, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_layout(actions, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * rescan_btn = lv_btn_create(actions);
    lv_obj_set_size(rescan_btn, 220, 52);
    lv_obj_set_style_bg_color(rescan_btn, lv_color_hex(CLR_SURFACE_BTN_TOP), LV_PART_MAIN);
    lv_obj_set_style_border_width(rescan_btn, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(rescan_btn, lv_color_hex(CLR_BORDER_SUBTLE), LV_PART_MAIN);
    lv_obj_set_style_radius(rescan_btn, 16, LV_PART_MAIN);
    lv_obj_add_event_cb(rescan_btn, doormount_rescan_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t * rescan_lbl = lv_label_create(rescan_btn);
    lv_label_set_text(rescan_lbl, "Rescan");
    lv_obj_set_style_text_font(rescan_lbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(rescan_lbl, lv_color_hex(CLR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_center(rescan_lbl);

    lv_obj_t * close_btn = lv_btn_create(actions);
    lv_obj_set_size(close_btn, 220, 52);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x9B3B3B), LV_PART_MAIN);
    lv_obj_set_style_radius(close_btn, 16, LV_PART_MAIN);
    lv_obj_set_style_border_width(close_btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(close_btn, doormount_close_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t * close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "Close");
    lv_obj_set_style_text_font(close_lbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(close_lbl, lv_color_hex(CLR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_center(close_lbl);

    doormount_scan_and_render();
}

static void reset_confirm_event(lv_event_t * e) {
    (void)e;

    reset_popup_close();

    app_state_set_status("Resetting network credentials...");
    refresh_footer_label();

    if (!device_config_factory_reset()) {
        app_state_set_status("Reset failed: config write error");
        refresh_footer_label();
        return;
    }

    bool provisioning_started = provisioning_service_restart_for_reprovision();
    const char * softap_ssid = provisioning_started ? provisioning_service_get_softap_ssid() : "PiSetup-XXXX";

    ui_show_provisioning_screen(softap_ssid);
}

static void reset_cancel_event(lv_event_t * e) {
    (void)e;
    reset_popup_close();
}

static void show_reset_confirm_popup(void) {
    reset_popup_close();

    lv_obj_t * screen = lv_scr_act();
    if (screen == NULL) {
        return;
    }

    g_reset_overlay = lv_obj_create(screen);
    lv_obj_remove_style_all(g_reset_overlay);
    lv_obj_set_size(g_reset_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(g_reset_overlay, 0, 0);
    lv_obj_set_style_bg_color(g_reset_overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_reset_overlay, LV_OPA_70, LV_PART_MAIN);
    lv_obj_clear_flag(g_reset_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * panel = lv_obj_create(g_reset_overlay);
    lv_obj_set_size(panel, SETTINGS_POPUP_W, SETTINGS_POPUP_H);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, lv_color_hex(CLR_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_hex(CLR_BORDER_SUBTLE), LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * title = lv_label_create(panel);
    lv_label_set_text(title, "Reset Network Credentials?");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t * body = lv_label_create(panel);
    lv_obj_set_width(body, SETTINGS_POPUP_W - 80);
    lv_label_set_text(body, "This clears Wi-Fi + userId and returns to device setup QR mode.");
    lv_obj_set_style_text_font(body, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(body, lv_color_hex(CLR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_align(body, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t * actions = lv_obj_create(panel);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, lv_pct(100), 96);
    lv_obj_align(actions, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_layout(actions, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * cancel_btn = lv_btn_create(actions);
    lv_obj_set_size(cancel_btn, 250, 72);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(CLR_SURFACE_BTN_TOP), LV_PART_MAIN);
    lv_obj_set_style_border_width(cancel_btn, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(cancel_btn, lv_color_hex(CLR_BORDER_SUBTLE), LV_PART_MAIN);
    lv_obj_set_style_radius(cancel_btn, 18, LV_PART_MAIN);
    lv_obj_add_event_cb(cancel_btn, reset_cancel_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t * cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_set_style_text_font(cancel_lbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(cancel_lbl, lv_color_hex(CLR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_center(cancel_lbl);

    lv_obj_t * reset_btn = lv_btn_create(actions);
    lv_obj_set_size(reset_btn, 250, 72);
    lv_obj_set_style_bg_color(reset_btn, lv_color_hex(0x9B3B3B), LV_PART_MAIN);
    lv_obj_set_style_radius(reset_btn, 18, LV_PART_MAIN);
    lv_obj_set_style_border_width(reset_btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(reset_btn, reset_confirm_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t * reset_lbl = lv_label_create(reset_btn);
    lv_label_set_text(reset_lbl, "Reset");
    lv_obj_set_style_text_font(reset_lbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(reset_lbl, lv_color_hex(CLR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_center(reset_lbl);
}

static void settings_reset_event(lv_event_t * e) {
    (void)e;
    settings_popup_close();
    show_reset_confirm_popup();
}

static void settings_doormount_event(lv_event_t * e) {
    (void)e;
    settings_popup_close();
    show_doormount_popup();
}

static void settings_close_event(lv_event_t * e) {
    (void)e;
    settings_popup_close();
}

static void show_settings_popup(void) {
    settings_popup_close();

    lv_obj_t * screen = lv_scr_act();
    if (screen == NULL) {
        return;
    }

    g_settings_overlay = lv_obj_create(screen);
    lv_obj_remove_style_all(g_settings_overlay);
    lv_obj_set_size(g_settings_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(g_settings_overlay, 0, 0);
    lv_obj_set_style_bg_color(g_settings_overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_settings_overlay, LV_OPA_70, LV_PART_MAIN);
    lv_obj_clear_flag(g_settings_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * panel = lv_obj_create(g_settings_overlay);
    lv_obj_set_size(panel, SETTINGS_POPUP_W, SETTINGS_POPUP_H);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, lv_color_hex(CLR_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_hex(CLR_BORDER_SUBTLE), LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * title = lv_label_create(panel);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 22);

    lv_obj_t * reset_btn = lv_btn_create(panel);
    lv_obj_set_size(reset_btn, SETTINGS_POPUP_W - 90, 88);
    lv_obj_align(reset_btn, LV_ALIGN_TOP_MID, 0, 102);
    lv_obj_set_style_bg_color(reset_btn, lv_color_hex(CLR_SURFACE_BTN_TOP), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(reset_btn, lv_color_hex(CLR_SURFACE_BTN_BOT), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(reset_btn, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_border_width(reset_btn, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(reset_btn, lv_color_hex(CLR_BORDER_SUBTLE), LV_PART_MAIN);
    lv_obj_set_style_radius(reset_btn, 18, LV_PART_MAIN);
    lv_obj_add_event_cb(reset_btn, settings_reset_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t * reset_lbl = lv_label_create(reset_btn);
    lv_label_set_text(reset_lbl, "Reset Network Credentials/UserId");
    lv_obj_set_style_text_font(reset_lbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(reset_lbl, lv_color_hex(CLR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_center(reset_lbl);

    lv_obj_t * doormount_btn = lv_btn_create(panel);
    lv_obj_set_size(doormount_btn, SETTINGS_POPUP_W - 90, 88);
    lv_obj_align(doormount_btn, LV_ALIGN_TOP_MID, 0, 206);
    lv_obj_set_style_bg_color(doormount_btn, lv_color_hex(CLR_SURFACE_BTN_TOP), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(doormount_btn, lv_color_hex(CLR_SURFACE_BTN_BOT), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(doormount_btn, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_border_width(doormount_btn, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(doormount_btn, lv_color_hex(CLR_BORDER_SUBTLE), LV_PART_MAIN);
    lv_obj_set_style_radius(doormount_btn, 18, LV_PART_MAIN);
    lv_obj_add_event_cb(doormount_btn, settings_doormount_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t * doormount_lbl = lv_label_create(doormount_btn);
    lv_label_set_text(doormount_lbl, "Setup Doormount");
    lv_obj_set_style_text_font(doormount_lbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(doormount_lbl, lv_color_hex(CLR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_center(doormount_lbl);

    lv_obj_t * close_btn = lv_btn_create(panel);
    lv_obj_set_size(close_btn, SETTINGS_POPUP_W - 90, 54);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x9B3B3B), LV_PART_MAIN);
    lv_obj_set_style_radius(close_btn, 14, LV_PART_MAIN);
    lv_obj_set_style_border_width(close_btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(close_btn, settings_close_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t * close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "Close");
    lv_obj_set_style_text_font(close_lbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(close_lbl, lv_color_hex(CLR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_center(close_lbl);
}

static void settings_button_event(lv_event_t * e) {
    (void)e;
    show_settings_popup();
}

static void task_start_event(lv_event_t * e) {
    uint32_t idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    if (idx >= g_app_state.home_task_count) return;

    const HomeTask * task = &g_app_state.home_tasks[idx];
    const char * task_title = (task->title[0] != '\0') ? task->title : "this task";

    session_confirm_popup_show_task(task_title, (uint8_t)idx);
}

static void create_single_task_card(lv_obj_t * parent, uint8_t idx) {
    /* Card container */
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, TASK_CARD_H);
    lv_obj_set_style_bg_color(card, lv_color_hex(CLR_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(card, TASK_CARD_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(card, 16, LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(card, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(card, LV_OPA_40, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Green accent strip (left edge) */
    lv_obj_t * accent = lv_obj_create(card);
    lv_obj_set_size(accent, CARD_ACCENT_W, lv_pct(100));
    lv_obj_align(accent, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(accent, lv_color_hex(CLR_ACCENT_STRIP), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(accent, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(accent, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(accent, 30, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(accent, lv_color_hex(CLR_ACCENT_STRIP), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(accent, LV_OPA_70, LV_PART_MAIN);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE);

    /* START button (right side, full height, rounded) */
    lv_obj_t * start_slab = lv_obj_create(card);
    lv_obj_set_size(start_slab, CARD_START_BTN_W, lv_pct(100));
    lv_obj_align(start_slab, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(start_slab, lv_color_hex(CLR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(start_slab, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(start_slab, lv_color_hex(0x00ac90), LV_STATE_HOVERED | LV_PART_MAIN);
    lv_obj_set_style_border_width(start_slab, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(start_slab, CARD_START_BTN_R, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(start_slab, 30, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(start_slab, lv_color_hex(CLR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(start_slab, LV_OPA_70, LV_PART_MAIN);
    lv_obj_clear_flag(start_slab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(start_slab, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(start_slab, task_start_event, LV_EVENT_CLICKED, (void *)(uintptr_t)idx);

    lv_obj_t * start_lbl = lv_label_create(start_slab);
    lv_label_set_text(start_lbl, "START");
    lv_obj_set_style_text_font(start_lbl, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(start_lbl, lv_color_hex(CLR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(start_lbl, 4, LV_PART_MAIN);
    lv_obj_center(start_lbl);

    /* Body content area (between accent strip and START button) */
    lv_obj_t * body = lv_obj_create(card);
    lv_obj_remove_style_all(body);
    int32_t body_w = SCREEN_W - (TASK_AREA_PAD_X * 2) - CARD_ACCENT_W - CARD_START_BTN_W - CARD_BODY_PAD_L;
    lv_obj_set_size(body, body_w, lv_pct(100));
    lv_obj_align(body, LV_ALIGN_LEFT_MID, CARD_ACCENT_W + CARD_BODY_PAD_L, 0);

    /* Title — large, bold, truncated */
    lv_obj_t * title = lv_label_create(body);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_height(title, lv_font_get_line_height(&lv_font_montserrat_48));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, CARD_BODY_PAD_T);

    /* White time badge — full width bar with time range text */
    lv_obj_t * badge_cont = lv_obj_create(body);
    lv_obj_set_width(badge_cont, lv_pct(95));
    lv_obj_set_height(badge_cont, TIME_BADGE_H);
    lv_obj_set_style_bg_color(badge_cont, lv_color_hex(CLR_TIME_BADGE_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(badge_cont, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(badge_cont, TIME_BADGE_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_border_width(badge_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(badge_cont, TIME_BADGE_PAD_X, LV_PART_MAIN);
    lv_obj_set_style_pad_right(badge_cont, TIME_BADGE_PAD_X, LV_PART_MAIN);
    lv_obj_align(badge_cont, LV_ALIGN_TOP_LEFT, 0, CARD_BODY_PAD_T + 74);
    lv_obj_clear_flag(badge_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * time_lbl = lv_label_create(badge_cont);
    lv_obj_set_style_text_font(time_lbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(time_lbl, lv_color_hex(CLR_TIME_BADGE_TEXT), LV_PART_MAIN);
    lv_obj_align(time_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    /* Subtitle / description */
    lv_obj_t * subtitle = lv_label_create(body);
    lv_obj_set_width(subtitle, lv_pct(100));
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(CLR_TEXT_DESC), LV_PART_MAIN);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_DOT);
    lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 0, CARD_BODY_PAD_T + 122);

    /* Status label (bottom of body) */
    lv_obj_t * status = lv_label_create(body);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(status, lv_color_hex(0xA8E7B4), LV_PART_MAIN);
    lv_obj_align(status, LV_ALIGN_BOTTOM_LEFT, 0, -14);

    /* Store references */
    g_cards[idx].card       = card;
    g_cards[idx].title      = title;
    g_cards[idx].time_badge = time_lbl;
    g_cards[idx].subtitle   = subtitle;
    g_cards[idx].status     = status;
}

static void create_task_cards(lv_obj_t * parent) {
    for (uint8_t i = 0; i < HOME_CARD_POOL_SIZE; i++) {
        create_single_task_card(parent, i);
    }
}

/* -----------------------------------------------------------------------
 * Native LVGL Scrollbar Styling
 * (Replaced static carousel indicator)
 * ----------------------------------------------------------------------- */
static void apply_list_scrollbar_style(lv_obj_t * list) {
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(list, lv_color_hex(CLR_DOT_ACTIVE), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list, LV_OPA_50, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(list, 10, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(list, 8, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(list, 8, LV_PART_SCROLLBAR);
}

/* -----------------------------------------------------------------------
 * screen_home_create — main entry point
 * Reconstructs the React Dashboard layout at 800x480:
 *   ┌─────────────────────────────────────────┐
 *   │  HH : MM          [ ▶ Quick Focus ]     │  ← header
 *   │  SUN, 22 MAR                            │
 *   ├─────────────────────────────────────────┤
 *   │  ┌─────────────────────────────────┐  · │  ← task carousel
 *   │  │ [▌] Title            [ START ]  │  █ │
 *   │  │     ┌─ 09:00 – 10:30 ──────┐   │  · │
 *   │  │     Description             │   │    │
 *   │  └─────────────────────────────────┘    │
 *   ├─────────────────────────────────────────┤
 *   │  Ready                                  │  ← footer
 *   └─────────────────────────────────────────┘
 * ----------------------------------------------------------------------- */
lv_obj_t * screen_home_create(void) {
    int32_t sw = SCREEN_W;
    int32_t sh = SCREEN_H;

    /* Task area geometry */
    int32_t task_y = HEADER_H;
    int32_t task_h = sh - HEADER_H - FOOTER_H;
    int32_t quick_btn_x = sw - QF_BTN_W - HEADER_PAD_RIGHT;
    int32_t settings_btn_x = quick_btn_x - HEADER_CTRL_GAP - SETTINGS_BTN_SIZE;
    int32_t time_col_w = settings_btn_x - HEADER_PAD_LEFT - HEADER_CTRL_GAP;
    if (time_col_w < 320) {
        time_col_w = 320;
    }

    g_settings_overlay = NULL;
    g_reset_overlay = NULL;
    g_doormount_overlay = NULL;
    g_doormount_status = NULL;
    g_doormount_list = NULL;
    if (g_doormount_setup_timer != NULL) {
        lv_timer_delete(g_doormount_setup_timer);
        g_doormount_setup_timer = NULL;
    }
    g_doormount_setup_inflight = false;
    g_doormount_setup_done = false;
    g_doormount_setup_success = false;
    g_doormount_setup_error[0] = '\0';
    g_doormount_selected_ssid[0] = '\0';
    memset(&g_doormount_networks, 0, sizeof(g_doormount_networks));

    /* ── Screen ── */
    lv_obj_t * screen = lv_obj_create(NULL);
    lv_obj_set_size(screen, sw, sh);
    lv_obj_set_style_bg_color(screen, lv_color_hex(CLR_BG_TOP), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(CLR_BG_BOTTOM), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Clock column (top-left) ── */
    lv_obj_t * time_col = lv_obj_create(screen);
    lv_obj_remove_style_all(time_col);
    lv_obj_set_size(time_col, time_col_w, HEADER_H - HEADER_PAD_TOP);
    lv_obj_set_pos(time_col, HEADER_PAD_LEFT, HEADER_PAD_TOP + 25);
    lv_obj_add_flag(time_col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(time_col, LV_OBJ_FLAG_PRESS_LOCK);

    g_lbl_time = lv_label_create(time_col);
    lv_obj_add_flag(g_lbl_time, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_lbl_time, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_set_width(g_lbl_time, time_col_w); /* Keep large clock font visible without overrun on 800px screen */
    lv_obj_set_style_text_font(g_lbl_time, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_lbl_time, lv_color_hex(CLR_TEXT_CLOCK), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(g_lbl_time, CLOCK_LETTER_SPACE, LV_PART_MAIN);
    lv_obj_align(g_lbl_time, LV_ALIGN_TOP_LEFT, 0, 0);

    g_lbl_date = lv_label_create(time_col);
    lv_obj_add_flag(g_lbl_date, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_lbl_date, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_set_style_text_font(g_lbl_date, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_lbl_date, lv_color_hex(CLR_TEXT_DATE), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(g_lbl_date, 7, LV_PART_MAIN);
    lv_obj_align_to(g_lbl_date, g_lbl_time, LV_ALIGN_OUT_BOTTOM_LEFT, 2, 8);

    g_pull_spinner = lv_spinner_create(screen);
    lv_obj_set_size(g_pull_spinner, PULL_REFRESH_SPINNER_SIZE, PULL_REFRESH_SPINNER_SIZE);
    lv_spinner_set_anim_params(g_pull_spinner, 700, 90);
    lv_obj_set_pos(g_pull_spinner, PULL_REFRESH_SPINNER_X, PULL_REFRESH_SPINNER_BASE_Y);
    lv_obj_set_style_arc_width(g_pull_spinner, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(g_pull_spinner, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_pull_spinner, lv_color_hex(CLR_TEXT_SECONDARY), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_pull_spinner, lv_color_hex(CLR_BORDER_SUBTLE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_pull_spinner, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(g_pull_spinner, LV_OBJ_FLAG_CLICKABLE);
    pull_refresh_hide_spinner();

    lv_obj_add_event_cb(time_col,  time_pull_refresh_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(time_col,  time_pull_refresh_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(time_col,  time_pull_refresh_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(time_col,  time_pull_refresh_event_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(g_lbl_time, time_pull_refresh_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(g_lbl_time, time_pull_refresh_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(g_lbl_time, time_pull_refresh_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(g_lbl_time, time_pull_refresh_event_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(g_lbl_date, time_pull_refresh_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(g_lbl_date, time_pull_refresh_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(g_lbl_date, time_pull_refresh_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(g_lbl_date, time_pull_refresh_event_cb, LV_EVENT_PRESS_LOST, NULL);

    /* ── Quick Focus button (top-right) ── */
    lv_obj_t * quick_btn = lv_btn_create(screen);
    lv_obj_set_size(quick_btn, QF_BTN_W, QF_BTN_H);
    lv_obj_set_pos(quick_btn, quick_btn_x, HEADER_PAD_TOP);
    lv_obj_set_style_bg_color(quick_btn, lv_color_hex(CLR_SURFACE_BTN_TOP), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(quick_btn, lv_color_hex(CLR_SURFACE_BTN_BOT), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(quick_btn, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_border_width(quick_btn, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(quick_btn, lv_color_hex(CLR_BORDER_SUBTLE), LV_PART_MAIN);
    lv_obj_set_style_radius(quick_btn, QF_BTN_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(quick_btn, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(quick_btn, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(quick_btn, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(quick_btn, 25, LV_STATE_HOVERED | LV_PART_MAIN);
    lv_obj_set_style_shadow_color(quick_btn, lv_color_hex(0x10B981), LV_STATE_HOVERED | LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(quick_btn, LV_OPA_70, LV_STATE_HOVERED | LV_PART_MAIN);
    lv_obj_add_event_cb(quick_btn, quick_focus_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t * quick_lbl = lv_label_create(quick_btn);
    lv_label_set_text(quick_lbl, LV_SYMBOL_PLAY " Quick Focus");
    lv_obj_set_style_text_font(quick_lbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(quick_lbl, lv_color_hex(CLR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_center(quick_lbl);
    lv_obj_set_style_pad_left(quick_lbl, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_right(quick_lbl, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_top(quick_lbl, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(quick_lbl, 12, LV_PART_MAIN);

    /* ── Settings button (between clock + Quick Focus) ── */
    lv_obj_t * settings_btn = lv_btn_create(screen);
    lv_obj_set_size(settings_btn, SETTINGS_BTN_SIZE, SETTINGS_BTN_SIZE);
    lv_obj_set_pos(settings_btn,
                   settings_btn_x,
                   HEADER_PAD_TOP + ((QF_BTN_H - SETTINGS_BTN_SIZE) / 2));
    lv_obj_set_style_bg_color(settings_btn, lv_color_hex(CLR_SURFACE_BTN_TOP), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(settings_btn, lv_color_hex(CLR_SURFACE_BTN_BOT), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(settings_btn, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_border_width(settings_btn, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(settings_btn, lv_color_hex(CLR_BORDER_SUBTLE), LV_PART_MAIN);
    lv_obj_set_style_radius(settings_btn, 22, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(settings_btn, 18, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(settings_btn, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(settings_btn, LV_OPA_30, LV_PART_MAIN);
    lv_obj_add_event_cb(settings_btn, settings_button_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t * settings_lbl = lv_label_create(settings_btn);
    lv_label_set_text(settings_lbl, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(settings_lbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(settings_lbl, lv_color_hex(CLR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_center(settings_lbl);

    /* ── Task list (flex column, scrollable) ── */
    g_task_list = lv_obj_create(screen);
    lv_obj_set_size(g_task_list, sw - (TASK_AREA_PAD_X * 2) - DOT_RIGHT_MARGIN, task_h);
    lv_obj_set_pos(g_task_list, TASK_AREA_PAD_X, task_y);
    lv_obj_set_style_bg_opa(g_task_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_task_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(g_task_list, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(g_task_list, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_left(g_task_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(g_task_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(g_task_list, TASK_CARD_GAP, LV_PART_MAIN);
    lv_obj_set_scroll_dir(g_task_list, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(g_task_list, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_layout(g_task_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(g_task_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_task_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    apply_list_scrollbar_style(g_task_list);
    lv_obj_add_event_cb(g_task_list, task_list_scroll_cb, LV_EVENT_SCROLL, NULL);

    /* Empty state label */
    g_lbl_empty_state = lv_label_create(screen);
    lv_obj_set_style_text_font(g_lbl_empty_state, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_lbl_empty_state, lv_color_hex(CLR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_set_style_text_align(g_lbl_empty_state, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(g_lbl_empty_state, LV_ALIGN_CENTER, 0, 40);
    lv_obj_add_flag(g_lbl_empty_state, LV_OBJ_FLAG_HIDDEN);

    create_task_cards(g_task_list);

    /* ── Footer status label ── */
    g_lbl_footer = lv_label_create(screen);
    lv_obj_set_style_text_font(g_lbl_footer, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_lbl_footer, lv_color_hex(CLR_TEXT_MUTED), LV_PART_MAIN);
    lv_obj_set_pos(g_lbl_footer, HEADER_PAD_LEFT, sh - FOOTER_H - 30);
    lv_label_set_text(g_lbl_footer, "Ready");

    /* ── Wire up global state ── */
    g_lbl_status = g_lbl_footer;
    app_state_set_status("Ready");
    app_state_set_tasks_loading(true);
    session_confirm_popup_set_start_cb(start_session_from_popup);

    /* Force first label render for newly created Home screen instances. */
    memset(g_last_time, 0, sizeof(g_last_time));
    memset(g_last_date, 0, sizeof(g_last_date));
    g_pull_tracking = false;
    g_pull_start_y = 0;
    g_pull_delta_y = 0;
    g_last_manual_refresh_tick = lv_tick_get() - PULL_REFRESH_COOLDOWN_MS;

    /* ── Kick off timers and initial fetch ── */
    update_clock_labels();
    render_task_cards();

    if (g_clock_timer != NULL) {
        lv_timer_delete(g_clock_timer);
        g_clock_timer = NULL;
    }
    if (g_refresh_timer != NULL) {
        lv_timer_delete(g_refresh_timer);
        g_refresh_timer = NULL;
    }

    g_clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
    g_refresh_timer = lv_timer_create(refresh_timer_cb, HOME_API_REFRESH_MS, NULL);

    start_due_today_fetch(false);

    return screen;
}
