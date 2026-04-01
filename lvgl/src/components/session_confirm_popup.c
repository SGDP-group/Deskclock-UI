#include "session_confirm_popup.h"

#include <string.h>
#include <stdio.h>

#define POPUP_W 620
#define POPUP_H 360
#define ACTION_ROW_H 110

#define ANIM_ENTER_MS 220
#define ANIM_EXIT_MS 170

#define CLR_OVERLAY 0x000000
#define CLR_PANEL_BG 0x1A1A1E
#define CLR_PANEL_BORDER 0x363A40
#define CLR_TEXT 0xFFFFFF
#define CLR_CANCEL_BG 0x9B3B3B
#define CLR_START_BG 0x3A9778

static lv_obj_t * s_overlay = NULL;
static lv_obj_t * s_panel = NULL;
static lv_obj_t * s_body_label = NULL;
static lv_obj_t * s_task_title_label = NULL;
static bool s_closing = false;
static session_confirm_start_cb_t s_start_cb = NULL;
static SessionConfirmKind s_kind = SESSION_CONFIRM_KIND_QUICK;
static uint8_t s_task_index = 0xFF;

static void popup_delete_now(void) {
    if (s_overlay != NULL) {
        lv_obj_del(s_overlay);
    }
    s_overlay = NULL;
    s_panel = NULL;
    s_body_label = NULL;
    s_task_title_label = NULL;
    s_closing = false;
}

static void anim_set_overlay_opa(void * obj, int32_t v) {
    lv_obj_set_style_bg_opa((lv_obj_t *)obj, (lv_opa_t)v, LV_PART_MAIN);
}

static void anim_set_panel_opa(void * obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, LV_PART_MAIN);
}

static void anim_set_panel_translate_y(void * obj, int32_t v) {
    lv_obj_set_style_translate_y((lv_obj_t *)obj, v, LV_PART_MAIN);
}

static void close_anim_done_cb(lv_anim_t * a) {
    (void)a;
    popup_delete_now();
}

static void start_show_animation(void) {
    if (s_overlay == NULL || s_panel == NULL) return;

    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_opa(s_panel, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_translate_y(s_panel, 30, LV_PART_MAIN);

    lv_anim_t a;

    lv_anim_init(&a);
    lv_anim_set_var(&a, s_overlay);
    lv_anim_set_exec_cb(&a, anim_set_overlay_opa);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_70);
    lv_anim_set_duration(&a, ANIM_ENTER_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, s_panel);
    lv_anim_set_exec_cb(&a, anim_set_panel_opa);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, ANIM_ENTER_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, s_panel);
    lv_anim_set_exec_cb(&a, anim_set_panel_translate_y);
    lv_anim_set_values(&a, 30, 0);
    lv_anim_set_duration(&a, ANIM_ENTER_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void session_confirm_popup_close(void) {
    if (s_overlay == NULL || s_panel == NULL || s_closing) return;

    s_closing = true;

    lv_anim_t a;

    lv_anim_init(&a);
    lv_anim_set_var(&a, s_overlay);
    lv_anim_set_exec_cb(&a, anim_set_overlay_opa);
    lv_anim_set_values(&a, LV_OPA_70, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, ANIM_EXIT_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, s_panel);
    lv_anim_set_exec_cb(&a, anim_set_panel_opa);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, ANIM_EXIT_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, s_panel);
    lv_anim_set_exec_cb(&a, anim_set_panel_translate_y);
    lv_anim_set_values(&a, 0, 16);
    lv_anim_set_duration(&a, ANIM_EXIT_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, close_anim_done_cb);
    lv_anim_start(&a);
}

static void cancel_btn_event_cb(lv_event_t * e) {
    (void)e;
    session_confirm_popup_close();
}

static void start_btn_event_cb(lv_event_t * e) {
    (void)e;

    if (s_start_cb != NULL) {
        s_start_cb(s_kind, s_task_index);
    }

    session_confirm_popup_close();
}

void session_confirm_popup_set_start_cb(session_confirm_start_cb_t cb) {
    s_start_cb = cb;
}

static void create_popup_shell(void) {
    lv_obj_t * screen = lv_scr_act();
    if (screen == NULL) return;

    if (s_overlay != NULL) {
        popup_delete_now();
    }

    s_overlay = lv_obj_create(screen);
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(CLR_OVERLAY), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_70, LV_PART_MAIN);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    s_panel = lv_obj_create(s_overlay);
    lv_obj_set_size(s_panel, POPUP_W, POPUP_H);
    lv_obj_center(s_panel);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(CLR_PANEL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(CLR_PANEL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_radius(s_panel, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(s_panel, true, LV_PART_MAIN);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * title = lv_label_create(s_panel);
    lv_label_set_text(title, "Start Session?");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_TEXT), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    s_body_label = lv_label_create(s_panel);
    lv_obj_set_width(s_body_label, POPUP_W - 80);
    lv_obj_set_style_text_font(s_body_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_body_label, lv_color_hex(CLR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_body_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(s_body_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_body_label, LV_ALIGN_CENTER, 0, 0);

    s_task_title_label = lv_label_create(s_panel);
    lv_obj_set_width(s_task_title_label, POPUP_W - 110);
    lv_obj_set_height(s_task_title_label, lv_font_get_line_height(&lv_font_montserrat_24) * 2);
    lv_obj_set_style_text_font(s_task_title_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_task_title_label, lv_color_hex(CLR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_task_title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(s_task_title_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_task_title_label, LV_ALIGN_CENTER, 0, 22);
    lv_obj_add_flag(s_task_title_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t * actions = lv_obj_create(s_panel);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, lv_pct(100), ACTION_ROW_H);
    lv_obj_align(actions, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_layout(actions, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * cancel_btn = lv_btn_create(actions);
    lv_obj_set_size(cancel_btn, lv_pct(50), lv_pct(100));
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(CLR_CANCEL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cancel_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0xff5151), LV_STATE_HOVERED | LV_PART_MAIN);
    lv_obj_set_style_shadow_width(cancel_btn, 20, LV_STATE_HOVERED | LV_PART_MAIN);
    lv_obj_set_style_shadow_color(cancel_btn, lv_color_hex(0xff5151), LV_STATE_HOVERED | LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(cancel_btn, LV_OPA_60, LV_STATE_HOVERED | LV_PART_MAIN);
    lv_obj_set_style_border_width(cancel_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(cancel_btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(cancel_btn, cancel_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_set_style_text_font(cancel_lbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(cancel_lbl, lv_color_hex(CLR_TEXT), LV_PART_MAIN);
    lv_obj_center(cancel_lbl);

    lv_obj_t * start_btn = lv_btn_create(actions);
    lv_obj_set_size(start_btn, lv_pct(50), lv_pct(100));
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(CLR_START_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(start_btn, LV_OPA_COVER, LV_PART_MAIN); 
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(0x00ac90), LV_STATE_HOVERED | LV_PART_MAIN);
    lv_obj_set_style_shadow_width(start_btn, 20, LV_STATE_HOVERED | LV_PART_MAIN);
    lv_obj_set_style_shadow_color(start_btn, lv_color_hex(0x00ac90), LV_STATE_HOVERED | LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(start_btn, LV_OPA_60, LV_STATE_HOVERED | LV_PART_MAIN);
    lv_obj_set_style_border_width(start_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(start_btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(start_btn, start_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * start_lbl = lv_label_create(start_btn);
    lv_label_set_text(start_lbl, "Start");
    lv_obj_set_style_text_font(start_lbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(start_lbl, lv_color_hex(CLR_TEXT), LV_PART_MAIN);
    lv_obj_center(start_lbl);
}

void session_confirm_popup_show_quick(void) {
    s_kind = SESSION_CONFIRM_KIND_QUICK;
    s_task_index = 0xFF;

    create_popup_shell();
    if (s_body_label != NULL) {
        lv_label_set_text(s_body_label, "Are you sure you want to start a quick session");
        lv_obj_align(s_body_label, LV_ALIGN_CENTER, 0, 0);
    }
    if (s_task_title_label != NULL) {
        lv_obj_add_flag(s_task_title_label, LV_OBJ_FLAG_HIDDEN);
    }
    start_show_animation();
}

void session_confirm_popup_show_task(const char * task_title, uint8_t task_index) {
    const char * safe_task_title = (task_title != NULL && task_title[0] != '\0') ? task_title : "this task";

    s_kind = SESSION_CONFIRM_KIND_TASK;
    s_task_index = task_index;

    create_popup_shell();
    if (s_body_label == NULL) return;

    lv_label_set_text(s_body_label, "Are you sure you want to start this session?");
    lv_obj_align(s_body_label, LV_ALIGN_CENTER, 0, -42);

    if (s_task_title_label != NULL) {
        lv_label_set_text(s_task_title_label, safe_task_title);
        lv_obj_clear_flag(s_task_title_label, LV_OBJ_FLAG_HIDDEN);
    }

    start_show_animation();
}
