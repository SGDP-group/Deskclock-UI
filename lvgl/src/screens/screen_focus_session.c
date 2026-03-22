#include "screen_focus_session.h"

#include "../ui.h"
#include "../data/app_state.h"

#include <stdio.h>

#define CLR_BG             0x000000
#define CLR_TITLE          0xF2F2F2
#define CLR_TIMER          0xF8F8F8
#define CLR_RING           0x0DA7A1
#define CLR_LEFT_BTN       0x45A286
#define CLR_RIGHT_BTN      0xA8181D

#define SIDE_BTN_W         260
#define SIDE_BTN_H         760
#define SIDE_BTN_RADIUS    48

static lv_timer_t * s_countdown_timer = NULL;
static lv_obj_t * s_timer_label = NULL;
static lv_obj_t * s_pause_label = NULL;
static lv_obj_t * s_ring = NULL;
static bool s_paused = false;
static uint32_t s_total_seconds = 0;
static uint32_t s_remaining_seconds = 0;

static void update_timer_text(void) {
    if (s_timer_label == NULL) return;

    uint32_t minutes = s_remaining_seconds / 60;
    uint32_t seconds = s_remaining_seconds % 60;
    lv_label_set_text_fmt(s_timer_label, "%02lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);

    if (s_ring != NULL) {
        uint32_t elapsed = (s_total_seconds > s_remaining_seconds) ? (s_total_seconds - s_remaining_seconds) : 0;
        int32_t progress = 0;
        if (s_total_seconds > 0) {
            progress = (int32_t)((elapsed * 100U) / s_total_seconds);
        }
        lv_arc_set_value(s_ring, progress);
    }
}

static void cleanup_countdown_timer(void) {
    if (s_countdown_timer != NULL) {
        lv_timer_del(s_countdown_timer);
        s_countdown_timer = NULL;
    }
}

static void stop_and_return_home(void) {
    cleanup_countdown_timer();
    ui_navigate_home();
}

static void countdown_timer_cb(lv_timer_t * timer) {
    (void)timer;
    if (s_paused) return;

    if (s_remaining_seconds > 0) {
        s_remaining_seconds--;
    }

    update_timer_text();

    if (s_remaining_seconds == 0) {
        app_state_set_status("Session complete");
        stop_and_return_home();
    }
}

static void pause_toggle_event(lv_event_t * e) {
    (void)e;
    s_paused = !s_paused;

    if (s_pause_label != NULL) {
        lv_label_set_text(s_pause_label, s_paused ? LV_SYMBOL_PLAY "\nPLAY" : "PAUSE");
    }

    app_state_set_status(s_paused ? "Session paused" : "Session resumed");
}

static void stop_event(lv_event_t * e) {
    (void)e;
    app_state_set_status("Session stopped");
    stop_and_return_home();
}

lv_obj_t * screen_focus_session_create(const char * title, uint32_t total_seconds) {
    cleanup_countdown_timer();
    s_paused = false;

    if (total_seconds == 0) {
        total_seconds = 30U * 60U;
    }

    s_total_seconds = total_seconds;
    s_remaining_seconds = total_seconds;

    lv_obj_t * screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(CLR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * title_label = lv_label_create(screen);
    lv_label_set_text(title_label, (title != NULL && title[0] != '\0') ? title : "Quick Session");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(title_label, lv_color_hex(CLR_TITLE), LV_PART_MAIN);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t * pause_btn = lv_btn_create(screen);
    lv_obj_set_size(pause_btn, SIDE_BTN_W, SIDE_BTN_H);
    lv_obj_align(pause_btn, LV_ALIGN_LEFT_MID, 0, 42);
    lv_obj_set_style_bg_color(pause_btn, lv_color_hex(CLR_LEFT_BTN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pause_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(pause_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(pause_btn, SIDE_BTN_RADIUS, LV_PART_MAIN);
    lv_obj_add_event_cb(pause_btn, pause_toggle_event, LV_EVENT_CLICKED, NULL);

    s_pause_label = lv_label_create(pause_btn);
    lv_label_set_text(s_pause_label, "PAUSE");
    lv_obj_set_style_text_font(s_pause_label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_pause_label, lv_color_hex(CLR_TITLE), LV_PART_MAIN);
    lv_obj_center(s_pause_label);

    lv_obj_t * stop_btn = lv_btn_create(screen);
    lv_obj_set_size(stop_btn, SIDE_BTN_W, SIDE_BTN_H);
    lv_obj_align(stop_btn, LV_ALIGN_RIGHT_MID, 0, 42);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(CLR_RIGHT_BTN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(stop_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(stop_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(stop_btn, SIDE_BTN_RADIUS, LV_PART_MAIN);
    lv_obj_add_event_cb(stop_btn, stop_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t * stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, LV_SYMBOL_STOP "\nSTOP");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(stop_label, lv_color_hex(CLR_TITLE), LV_PART_MAIN);
    lv_obj_center(stop_label);

    s_ring = lv_arc_create(screen);
    lv_obj_set_size(s_ring, 900, 900);
    lv_obj_align(s_ring, LV_ALIGN_CENTER, 0, 80);
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
    lv_obj_align(s_timer_label, LV_ALIGN_CENTER, 0, 80);

    update_timer_text();

    s_countdown_timer = lv_timer_create(countdown_timer_cb, 1000, NULL);

    app_state_set_status("Session running");
    return screen;
}
