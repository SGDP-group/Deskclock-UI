#include "screen_camera_preview.h"

#include "../ui.h"
#include "../data/app_state.h"
#include "src/focus_camera_capture.h"

#include <stdio.h>
#include <string.h>

#define PREVIEW_W 640
#define PREVIEW_H 480
#define PREVIEW_FRAME_BYTES (PREVIEW_W * PREVIEW_H * 2U)

#define CLR_BG 0x000000
#define CLR_TEXT 0xF2F2F2
#define CLR_GO_BACK 0x7A2E2E
#define CLR_START 0x2C8A69

static lv_obj_t * s_canvas = NULL;
static lv_obj_t * s_status = NULL;
static lv_timer_t * s_timer = NULL;

static bool s_is_quick = false;
static int s_task_id = -1;
static uint32_t s_duration_seconds = 0;
static char s_title[96] = {0};

static uint8_t s_canvas_buf[PREVIEW_FRAME_BYTES];
static uint8_t s_frame_copy_buf[PREVIEW_FRAME_BYTES];
static uint32_t s_last_seq = 0;

static void cleanup_preview_timer(void) {
    if (s_timer != NULL) {
        lv_timer_del(s_timer);
        s_timer = NULL;
    }
}

static void preview_timer_cb(lv_timer_t * timer) {
    (void)timer;

    uint32_t w = 0;
    uint32_t h = 0;
    uint32_t seq = 0;

    if (!focus_camera_capture_copy_latest_preview_rgb565(s_frame_copy_buf,
                                                          sizeof(s_frame_copy_buf),
                                                          &w,
                                                          &h,
                                                          &seq)) {
        app_state_set_status("Preview waiting for camera frame...");
        return;
    }

    if (w != PREVIEW_W || h != PREVIEW_H) {
        app_state_set_status("Preview frame size mismatch");
        return;
    }

    if (seq == s_last_seq) {
        return;
    }

    memcpy(s_canvas_buf, s_frame_copy_buf, PREVIEW_FRAME_BYTES);
    s_last_seq = seq;

    if (s_canvas != NULL) {
        lv_obj_invalidate(s_canvas);
    }

    char msg[96];
    snprintf(msg, sizeof(msg), "Preview live (%lu)", (unsigned long)seq);
    app_state_set_status(msg);
    if (s_status != NULL) {
        lv_label_set_text(s_status, msg);
    }
}

static void go_back_event(lv_event_t * e) {
    (void)e;
    cleanup_preview_timer();
    focus_camera_capture_stop();
    ui_navigate_home();
}

static void start_session_event(lv_event_t * e) {
    (void)e;
    cleanup_preview_timer();
    focus_camera_capture_stop();
    ui_navigate_focus_session(s_title, s_duration_seconds, s_is_quick, s_task_id);
}

lv_obj_t * screen_camera_preview_create(const char * title, uint32_t duration_seconds, bool is_quick_session, int task_id) {
    cleanup_preview_timer();

    s_is_quick = is_quick_session;
    s_task_id = task_id;
    s_duration_seconds = duration_seconds;
    s_last_seq = 0;

    memset(s_title, 0, sizeof(s_title));
    if (title != NULL && title[0] != '\0') {
        strncpy(s_title, title, sizeof(s_title) - 1U);
    } else {
        strncpy(s_title, "Focus Session", sizeof(s_title) - 1U);
    }

    lv_obj_t * screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(CLR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * title_lbl = lv_label_create(screen);
    lv_label_set_text_fmt(title_lbl, "Adjust Camera - %s", s_title);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(CLR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_align(title_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(title_lbl, LV_ALIGN_TOP_MID, 0, 8);

    s_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(s_canvas, s_canvas_buf, PREVIEW_W, PREVIEW_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_align(s_canvas, LV_ALIGN_CENTER, 0, 20);

    lv_obj_t * go_back_btn = lv_btn_create(screen);
    lv_obj_set_size(go_back_btn, 180, 64);
    lv_obj_align(go_back_btn, LV_ALIGN_BOTTOM_LEFT, 24, -12);
    lv_obj_set_style_bg_color(go_back_btn, lv_color_hex(CLR_GO_BACK), LV_PART_MAIN);
    lv_obj_set_style_border_width(go_back_btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(go_back_btn, go_back_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t * go_back_lbl = lv_label_create(go_back_btn);
    lv_label_set_text(go_back_lbl, "Go Back");
    lv_obj_set_style_text_font(go_back_lbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(go_back_lbl);

    lv_obj_t * start_btn = lv_btn_create(screen);
    lv_obj_set_size(start_btn, 180, 64);
    lv_obj_align(start_btn, LV_ALIGN_BOTTOM_RIGHT, -24, -12);
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(CLR_START), LV_PART_MAIN);
    lv_obj_set_style_border_width(start_btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(start_btn, start_session_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t * start_lbl = lv_label_create(start_btn);
    lv_label_set_text(start_lbl, "Start");
    lv_obj_set_style_text_font(start_lbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(start_lbl);

    s_status = lv_label_create(screen);
    lv_label_set_text(s_status, "Starting preview...");
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_status, lv_color_hex(CLR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -86);

    g_lbl_status = s_status;

    if (!focus_camera_capture_start_preview()) {
        app_state_set_status("Camera preview failed to start");
        lv_label_set_text(s_status, "Camera preview failed");
    } else {
        app_state_set_status("Preview ready. Adjust framing.");
        lv_label_set_text(s_status, "Preview ready. Adjust framing.");
    }

    s_timer = lv_timer_create(preview_timer_cb, 200, NULL);
    return screen;
}
