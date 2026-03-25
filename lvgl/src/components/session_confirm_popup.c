#include "session_confirm_popup.h"

#include <string.h>
#include <stdio.h>

#define POPUP_W 1260
#define POPUP_H 760
#define ACTION_ROW_H 230

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
static bool s_closing = false;

static void popup_delete_now(void) {
    if (s_overlay != NULL) {
        lv_obj_del(s_overlay);
    }
    s_overlay = NULL;
    s_panel = NULL;
    s_body_label = NULL;
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
    lv_obj_set_style_radius(s_panel, 34, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(s_panel, true, LV_PART_MAIN);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * title = lv_label_create(s_panel);
    lv_label_set_text(title, "Start Session?");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_TEXT), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 42);

    s_body_label = lv_label_create(s_panel);
    lv_obj_set_width(s_body_label, POPUP_W - 120);
    lv_obj_set_style_text_font(s_body_label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_body_label, lv_color_hex(CLR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_body_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(s_body_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_body_label, LV_ALIGN_TOP_MID, 0, 160);

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
    lv_obj_set_style_border_width(cancel_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(cancel_btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(cancel_btn, cancel_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_set_style_text_font(cancel_lbl, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(cancel_lbl, lv_color_hex(CLR_TEXT), LV_PART_MAIN);
    lv_obj_center(cancel_lbl);

    lv_obj_t * start_btn = lv_btn_create(actions);
    lv_obj_set_size(start_btn, lv_pct(50), lv_pct(100));
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(CLR_START_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(start_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(start_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(start_btn, 0, LV_PART_MAIN);

    lv_obj_t * start_lbl = lv_label_create(start_btn);
    lv_label_set_text(start_lbl, "Start");
    lv_obj_set_style_text_font(start_lbl, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(start_lbl, lv_color_hex(CLR_TEXT), LV_PART_MAIN);
    lv_obj_center(start_lbl);
}

void session_confirm_popup_show_quick(void) {
    create_popup_shell();
    if (s_body_label != NULL) {
        lv_label_set_text(s_body_label, "Are you sure you want to start a quick session");
    }
    start_show_animation();
}

void session_confirm_popup_show_task(const char * subtask_name) {
    char body[256];
    const char * safe_subtask = (subtask_name != NULL && subtask_name[0] != '\0') ? subtask_name : "this task";

    create_popup_shell();
    if (s_body_label == NULL) return;

    snprintf(body, sizeof(body), "Are you sure you want to start the session - %s now?", safe_subtask);
    body[sizeof(body) - 1] = '\0';
    lv_label_set_text(s_body_label, body);

    start_show_animation();
}
