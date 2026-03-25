#include "screen_focus_session.h"

#include "../ui.h"
#include "../data/app_state.h"
#include "src/home_config.h"
#include "src/focus_image_stream.h"
#include "src/focus_camera_capture.h"
#include "src/net_stream.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define CLR_BG             0x000000
#define CLR_TITLE          0xF2F2F2
#define CLR_TIMER          0xF8F8F8
#define CLR_RING           0x0DA7A1
#define CLR_LEFT_BTN       0x45A286
#define CLR_RIGHT_BTN      0xA8181D
#define CLR_POPUP_BG       0x1A1A1E
#define CLR_POPUP_CANCEL   0x9B3B3B
#define CLR_POPUP_CONFIRM  0x3A9778
#define CLR_POPUP_MIDDLE   0xA67C00

#define SIDE_BTN_W         120
#define SIDE_BTN_H         300
#define SIDE_BTN_RADIUS    24

#define POPUP_W            640
#define POPUP_H            380

typedef enum {
    PHASE_FOCUS = 0,
    PHASE_BREAK = 1,
    PHASE_WAITING_POPUP = 2,
} SessionPhase;

static lv_timer_t * s_countdown_timer = NULL;
static lv_obj_t * s_timer_label = NULL;
static lv_obj_t * s_pause_label = NULL;
static lv_obj_t * s_ring = NULL;
static lv_obj_t * s_title_label = NULL;
static lv_obj_t * s_pause_btn = NULL;
static lv_obj_t * s_popup_overlay = NULL;

static bool s_paused = false;
static bool s_is_quick = false;
static bool s_bonus_focus = false;
static SessionPhase s_phase = PHASE_FOCUS;

static uint32_t s_phase_total_seconds = 0;
static uint32_t s_phase_remaining_seconds = 0;
static uint32_t s_task_remaining_seconds = 0;
static int s_task_id = -1;
static uint32_t s_diag_tick_counter = 0;
static uint32_t s_no_frame_ticks = 0;
static uint32_t s_prev_frames_captured = 0;
static uint32_t s_prev_frames_enqueued = 0;
static char s_session_title[96] = {0};

static void update_runtime_diagnostics_status(void) {
    FocusImageStreamStats stream_stats = focus_image_stream_get_stats();
    FocusCameraCaptureStats cam_stats = focus_camera_capture_get_stats();

    const uint32_t cap_delta = cam_stats.frames_captured - s_prev_frames_captured;
    const uint32_t tx_delta = stream_stats.frames_enqueued - s_prev_frames_enqueued;

    const char * cam_state = "STARTING";
    const char * link_state = "DISCONNECTED";
    const char * send_state = "IDLE";
    char msg[196];

    if (cam_stats.camera_ready) {
        cam_state = cam_stats.paused ? "PAUSED" : "CAPTURING";
    } else if (cam_stats.capture_failures > 0U) {
        cam_state = "ERROR";
    }

    if (stream_stats.connected) {
        link_state = "CONNECTED";
    } else if (net_stream_fail_streak() > 0U) {
        link_state = "RETRYING";
    }

    if (cam_stats.paused || s_phase != PHASE_FOCUS) {
        send_state = "IDLE";
    } else if (!cam_stats.camera_ready) {
        send_state = "WAIT_CAMERA";
    } else if (tx_delta > 0U) {
        send_state = "SENDING";
    } else if (!stream_stats.connected) {
        send_state = "WAIT_LINK";
    } else {
        send_state = "WAIT_FRAME";
    }

    if (cam_stats.frames_captured == 0U && !cam_stats.camera_ready) {
        s_no_frame_ticks++;
    } else {
        s_no_frame_ticks = 0U;
    }

    if (cam_stats.frames_captured == 0U && cam_stats.capture_failures > 0U) {
        snprintf(msg,
                 sizeof(msg),
                 "Cam:%s Link:%s Send:%s\ncam_err:%s",
                 cam_state,
                 link_state,
                 send_state,
                 cam_stats.last_error);
    } else {
        snprintf(msg,
                 sizeof(msg),
                 "Cam:%s Link:%s Send:%s\ncap:%lu tx:%lu rej:%lu q:%lu",
                 cam_state,
                 link_state,
                 send_state,
                 (unsigned long)cam_stats.frames_captured,
                 (unsigned long)stream_stats.frames_enqueued,
                 (unsigned long)stream_stats.frames_rejected,
                 (unsigned long)stream_stats.queue_depth);
    }

    s_prev_frames_captured = cam_stats.frames_captured;
    s_prev_frames_enqueued = stream_stats.frames_enqueued;
    app_state_set_status(msg);
}

static uint32_t min_u32(uint32_t a, uint32_t b) {
    return (a < b) ? a : b;
}

static void close_popup(void) {
    if (s_popup_overlay != NULL) {
        lv_obj_del(s_popup_overlay);
        s_popup_overlay = NULL;
    }
}

static void update_timer_text(void) {
    if (s_timer_label == NULL) return;

    uint32_t minutes = s_phase_remaining_seconds / 60;
    uint32_t seconds = s_phase_remaining_seconds % 60;
    lv_label_set_text_fmt(s_timer_label, "%02lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);

    if (s_ring != NULL) {
        uint32_t elapsed = (s_phase_total_seconds > s_phase_remaining_seconds)
                         ? (s_phase_total_seconds - s_phase_remaining_seconds)
                         : 0;
        int32_t progress = 0;
        if (s_phase_total_seconds > 0) {
            progress = (int32_t)((elapsed * 100U) / s_phase_total_seconds);
        }
        lv_arc_set_value(s_ring, progress);
    }
}

static void set_controls_for_focus(bool focus_active) {
    if (s_pause_btn == NULL) return;

    if (focus_active) {
        lv_obj_clear_state(s_pause_btn, LV_STATE_DISABLED);
        if (s_pause_label != NULL) {
            lv_label_set_text(s_pause_label, s_paused ? LV_SYMBOL_PLAY "\nRESUME" : "PAUSE");
        }
    } else {
        lv_obj_add_state(s_pause_btn, LV_STATE_DISABLED);
        if (s_pause_label != NULL) {
            lv_label_set_text(s_pause_label, "PAUSE");
        }
    }
}

static void cleanup_countdown_timer(void) {
    if (s_countdown_timer != NULL) {
        lv_timer_del(s_countdown_timer);
        s_countdown_timer = NULL;
    }
    close_popup();
}

static void stop_and_return_home(void) {
    focus_camera_capture_stop();
    focus_image_stream_stop();
    cleanup_countdown_timer();
    ui_navigate_home();
}

static void start_focus_seconds(uint32_t seconds, bool bonus_focus) {
    s_phase = PHASE_FOCUS;
    s_bonus_focus = bonus_focus;
    s_paused = false;
    s_phase_total_seconds = seconds;
    s_phase_remaining_seconds = seconds;

    if (s_title_label != NULL) {
        lv_label_set_text(s_title_label, s_session_title);
    }

    set_controls_for_focus(true);
    focus_camera_capture_set_paused(false);
    focus_image_stream_set_paused(false);
    app_state_set_status("Session running");
    update_timer_text();
}

static void start_next_primary_focus(void) {
    if (s_is_quick) {
        start_focus_seconds((uint32_t)HOME_QUICK_SESSION_MINUTES * 60U, false);
        return;
    }

    if (s_task_remaining_seconds == 0U) {
        app_state_set_status("Session complete");
        stop_and_return_home();
        return;
    }

    uint32_t chunk_seconds = min_u32(s_task_remaining_seconds, (uint32_t)HOME_FOCUS_CHUNK_MINUTES * 60U);
    s_task_remaining_seconds -= chunk_seconds;
    start_focus_seconds(chunk_seconds, false);
}

static void start_break_countdown(void) {
    s_phase = PHASE_BREAK;
    s_phase_total_seconds = (uint32_t)HOME_BREAK_MINUTES * 60U;
    s_phase_remaining_seconds = s_phase_total_seconds;
    s_paused = false;

    if (s_title_label != NULL) {
        lv_label_set_text(s_title_label, "Break Time");
    }

    set_controls_for_focus(false);
    focus_camera_capture_set_paused(true);
    focus_image_stream_set_paused(true);
    app_state_set_status("Break started");
    update_timer_text();
}

static lv_obj_t * create_popup_container(void) {
    lv_obj_t * screen = lv_scr_act();
    if (screen == NULL) return NULL;

    close_popup();

    s_popup_overlay = lv_obj_create(screen);
    lv_obj_remove_style_all(s_popup_overlay);
    lv_obj_set_size(s_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_popup_overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_popup_overlay, LV_OPA_70, LV_PART_MAIN);

    lv_obj_t * popup = lv_obj_create(s_popup_overlay);
    lv_obj_set_size(popup, POPUP_W, POPUP_H);
    lv_obj_center(popup);
    lv_obj_set_style_bg_color(popup, lv_color_hex(CLR_POPUP_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(popup, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(popup, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(popup, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_all(popup, 0, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(popup, true, LV_PART_MAIN);
    lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);

    return popup;
}

static void popup_stop_event(lv_event_t * e) {
    (void)e;
    close_popup();
    app_state_set_status("Session stopped");
    stop_and_return_home();
}

static void popup_bonus_event(lv_event_t * e) {
    (void)e;
    close_popup();
    start_focus_seconds((uint32_t)HOME_BONUS_FOCUS_MINUTES * 60U, true);
}

static void popup_break_okay_event(lv_event_t * e) {
    (void)e;
    close_popup();
    start_break_countdown();
}

static void popup_resume_cancel_event(lv_event_t * e) {
    (void)e;
    close_popup();
    app_state_set_status("Session ended");
    stop_and_return_home();
}

static void popup_resume_start_event(lv_event_t * e) {
    (void)e;
    close_popup();
    start_next_primary_focus();
}

static void show_break_popup(void) {
    lv_obj_t * popup = create_popup_container();
    if (popup == NULL) return;

    lv_obj_t * title = lv_label_create(popup);
    lv_label_set_text(title, "Take a Break?");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_TITLE), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t * bonus_btn = lv_btn_create(popup);
    lv_obj_set_size(bonus_btn, lv_pct(100), 130);
    lv_obj_align(bonus_btn, LV_ALIGN_TOP_MID, 0, 82);
    lv_obj_set_style_bg_color(bonus_btn, lv_color_hex(CLR_POPUP_MIDDLE), LV_PART_MAIN);
    lv_obj_set_style_border_width(bonus_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bonus_btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(bonus_btn, popup_bonus_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t * bonus_label = lv_label_create(bonus_btn);
    lv_label_set_text(bonus_label, "+5 Minutes");
    lv_obj_set_style_text_font(bonus_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(bonus_label, lv_color_hex(CLR_TITLE), LV_PART_MAIN);
    lv_obj_center(bonus_label);

    lv_obj_t * actions = lv_obj_create(popup);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, lv_pct(100), 110);
    lv_obj_align(actions, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_layout(actions, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * stop_btn = lv_btn_create(actions);
    lv_obj_set_size(stop_btn, lv_pct(50), lv_pct(100));
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(CLR_POPUP_CANCEL), LV_PART_MAIN);
    lv_obj_set_style_border_width(stop_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(stop_btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(stop_btn, popup_stop_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t * stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, "Stop");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(stop_label, lv_color_hex(CLR_TITLE), LV_PART_MAIN);
    lv_obj_center(stop_label);

    lv_obj_t * okay_btn = lv_btn_create(actions);
    lv_obj_set_size(okay_btn, lv_pct(50), lv_pct(100));
    lv_obj_set_style_bg_color(okay_btn, lv_color_hex(CLR_POPUP_CONFIRM), LV_PART_MAIN);
    lv_obj_set_style_border_width(okay_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(okay_btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(okay_btn, popup_break_okay_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t * okay_label = lv_label_create(okay_btn);
    lv_label_set_text(okay_label, "Okay!");
    lv_obj_set_style_text_font(okay_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(okay_label, lv_color_hex(CLR_TITLE), LV_PART_MAIN);
    lv_obj_center(okay_label);

    s_phase = PHASE_WAITING_POPUP;
    app_state_set_status("Break options");
}

static void show_resume_popup(void) {
    lv_obj_t * popup = create_popup_container();
    if (popup == NULL) return;

    lv_obj_t * title = lv_label_create(popup);
    lv_label_set_text(title, "Resume Session?");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_TITLE), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 22);

    lv_obj_t * body = lv_label_create(popup);
    lv_obj_set_width(body, POPUP_W - 80);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(body, lv_color_hex(CLR_TITLE), LV_PART_MAIN);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(body, "%s", s_session_title);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 108);

    lv_obj_t * actions = lv_obj_create(popup);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, lv_pct(100), 110);
    lv_obj_align(actions, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_layout(actions, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * cancel_btn = lv_btn_create(actions);
    lv_obj_set_size(cancel_btn, lv_pct(50), lv_pct(100));
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(CLR_POPUP_CANCEL), LV_PART_MAIN);
    lv_obj_set_style_border_width(cancel_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(cancel_btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(cancel_btn, popup_resume_cancel_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t * cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(cancel_label, lv_color_hex(CLR_TITLE), LV_PART_MAIN);
    lv_obj_center(cancel_label);

    lv_obj_t * resume_btn = lv_btn_create(actions);
    lv_obj_set_size(resume_btn, lv_pct(50), lv_pct(100));
    lv_obj_set_style_bg_color(resume_btn, lv_color_hex(CLR_POPUP_CONFIRM), LV_PART_MAIN);
    lv_obj_set_style_border_width(resume_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(resume_btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(resume_btn, popup_resume_start_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t * resume_label = lv_label_create(resume_btn);
    lv_label_set_text(resume_label, "Resume");
    lv_obj_set_style_text_font(resume_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(resume_label, lv_color_hex(CLR_TITLE), LV_PART_MAIN);
    lv_obj_center(resume_label);

    s_phase = PHASE_WAITING_POPUP;
    app_state_set_status("Break complete");
}

static void handle_focus_finished(void) {
    if (s_bonus_focus) {
        s_bonus_focus = false;
        show_break_popup();
        return;
    }

    if (!s_is_quick && s_task_remaining_seconds == 0U) {
        app_state_set_status("Session complete");
        stop_and_return_home();
        return;
    }

    show_break_popup();
}

static void countdown_timer_cb(lv_timer_t * timer) {
    (void)timer;

    if (s_phase == PHASE_WAITING_POPUP) return;

    if (s_phase == PHASE_FOCUS && s_paused) {
        update_runtime_diagnostics_status();
        return;
    }

    if (s_phase_remaining_seconds > 0) {
        s_phase_remaining_seconds--;
    }

    update_timer_text();

    if (s_phase == PHASE_FOCUS && !s_paused) {
        s_diag_tick_counter++;
        update_runtime_diagnostics_status();
    }

    if (s_phase_remaining_seconds == 0) {
        if (s_phase == PHASE_FOCUS) {
            handle_focus_finished();
        } else if (s_phase == PHASE_BREAK) {
            show_resume_popup();
        }
    }
}

static void pause_toggle_event(lv_event_t * e) {
    (void)e;
    if (s_phase != PHASE_FOCUS) return;

    s_paused = !s_paused;
    focus_camera_capture_set_paused(s_paused);
    focus_image_stream_set_paused(s_paused);

    if (s_pause_label != NULL) {
        lv_label_set_text(s_pause_label, s_paused ? LV_SYMBOL_PLAY "\nRESUME" : "PAUSE");
    }

    app_state_set_status(s_paused ? "Session paused" : "Session resumed");
}

static void stop_event(lv_event_t * e) {
    (void)e;
    app_state_set_status("Session stopped");
    stop_and_return_home();
}

static bool start_session_stream_key(void) {
    if (!s_is_quick && s_task_id > 0) {
        return focus_image_stream_start_task(HOME_API_USER_ID, s_task_id);
    }

    char session_key[64];
    time_t now = time(NULL);
    struct tm local_tm;

#ifdef _WIN32
    localtime_s(&local_tm, &now);
#else
    localtime_r(&now, &local_tm);
#endif

    strftime(session_key, sizeof(session_key), "%Y%m%d_%H%M%S", &local_tm);

    char formatted_key[64];
    snprintf(formatted_key, sizeof(formatted_key), "%d_%s", HOME_API_USER_ID, session_key);
    return focus_image_stream_start_quick(HOME_API_USER_ID, formatted_key);
}

lv_obj_t * screen_focus_session_create(const char * title, uint32_t total_seconds, bool is_quick_session, int task_id) {
    cleanup_countdown_timer();
    s_diag_tick_counter = 0;
    s_no_frame_ticks = 0;
    s_prev_frames_captured = 0;
    s_prev_frames_enqueued = 0;

    memset(s_session_title, 0, sizeof(s_session_title));
    if (title != NULL && title[0] != '\0') {
        strncpy(s_session_title, title, sizeof(s_session_title) - 1);
    } else {
        strncpy(s_session_title, "Quick Session", sizeof(s_session_title) - 1);
    }

    s_is_quick = is_quick_session;
    s_task_id = task_id;
    s_task_remaining_seconds = s_is_quick ? 0U : total_seconds;

    bool stream_ok = start_session_stream_key();

    bool camera_ok = focus_camera_capture_start();
    focus_camera_capture_set_stream_enabled(true);

    if (total_seconds == 0) {
        total_seconds = (uint32_t)HOME_TASK_FALLBACK_MINUTES * 60U;
    }

    lv_obj_t * screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(CLR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    s_title_label = lv_label_create(screen);
    lv_label_set_text(s_title_label, s_session_title);
    lv_obj_set_style_text_font(s_title_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_title_label, lv_color_hex(CLR_TITLE), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_title_label, LV_ALIGN_TOP_MID, 0, 8);

    s_pause_btn = lv_btn_create(screen);
    lv_obj_set_size(s_pause_btn, SIDE_BTN_W, SIDE_BTN_H);
    lv_obj_align(s_pause_btn, LV_ALIGN_LEFT_MID, 60, 20);
    lv_obj_set_style_bg_color(s_pause_btn, lv_color_hex(CLR_LEFT_BTN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_pause_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_pause_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_pause_btn, SIDE_BTN_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(s_pause_btn, lv_color_hex(0x00FF00), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(s_pause_btn, 30, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_opa(s_pause_btn, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_event_cb(s_pause_btn, pause_toggle_event, LV_EVENT_CLICKED, NULL);

    s_pause_label = lv_label_create(s_pause_btn);
    lv_label_set_text(s_pause_label, "PAUSE");
    lv_obj_set_style_text_font(s_pause_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_pause_label, lv_color_hex(CLR_TITLE), LV_PART_MAIN);
    lv_obj_center(s_pause_label);

    lv_obj_t * stop_btn = lv_btn_create(screen);
    lv_obj_set_size(stop_btn, SIDE_BTN_W, SIDE_BTN_H);
    lv_obj_align(stop_btn, LV_ALIGN_RIGHT_MID, -60, 20);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(CLR_RIGHT_BTN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(stop_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(stop_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(stop_btn, SIDE_BTN_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(stop_btn, lv_color_hex(0xFF0000), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(stop_btn, 30, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_opa(stop_btn, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_event_cb(stop_btn, stop_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t * stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, LV_SYMBOL_STOP "\nSTOP");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(stop_label, lv_color_hex(CLR_TITLE), LV_PART_MAIN);
    lv_obj_center(stop_label);

    s_ring = lv_arc_create(screen);
    lv_obj_set_size(s_ring, 250, 250);
    lv_obj_align(s_ring, LV_ALIGN_CENTER, 0, 40);
    lv_arc_set_rotation(s_ring, 270);
    lv_arc_set_bg_angles(s_ring, 0, 360);
    lv_arc_set_range(s_ring, 0, 100);
    lv_arc_set_value(s_ring, 0);
    lv_obj_remove_style(s_ring, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(s_ring, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_ring, lv_color_hex(0x454545), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_ring, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_ring, lv_color_hex(CLR_RING), LV_PART_INDICATOR);
    lv_obj_clear_flag(s_ring, LV_OBJ_FLAG_CLICKABLE);

    s_timer_label = lv_label_create(screen);
    lv_obj_set_style_text_font(s_timer_label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_timer_label, lv_color_hex(CLR_TIMER), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(s_timer_label, 1, LV_PART_MAIN);
    lv_obj_align(s_timer_label, LV_ALIGN_CENTER, 0, 40);

    lv_obj_t * status_label = lv_label_create(screen);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(status_label, lv_color_hex(CLR_TITLE), LV_PART_MAIN);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -10);
    g_lbl_status = status_label;

    if (!stream_ok || !camera_ok) {
        FocusImageStreamStats stream_stats = focus_image_stream_get_stats();
        FocusCameraCaptureStats cam_stats = focus_camera_capture_get_stats();
        char boot_msg[128];
        snprintf(boot_msg,
                 sizeof(boot_msg),
                 "stream:%s cam:%s",
                 stream_ok ? "ok" : stream_stats.last_error,
                 camera_ok ? "ok" : cam_stats.last_error);
        app_state_set_status(boot_msg);
    }

    if (s_is_quick) {
        start_focus_seconds((uint32_t)HOME_QUICK_SESSION_MINUTES * 60U, false);
    } else {
        if (s_task_remaining_seconds == 0U) {
            s_task_remaining_seconds = (uint32_t)HOME_TASK_FALLBACK_MINUTES * 60U;
        }
        start_next_primary_focus();
    }

    s_countdown_timer = lv_timer_create(countdown_timer_cb, 1000, NULL);
    return screen;
}
