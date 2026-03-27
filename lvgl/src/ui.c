/**
 * ui.c — Root UI initializer
 * React analogy: this is your App.jsx
 *
 * All screen creation and initial navigation happens here.
 */

#include "ui.h"
#include "styles/theme.h"
#include "screens/screen_home.h"
#include "screens/screen_focus_session.h"
#include "screens/screen_camera_preview.h"
#include "lvgl/src/libs/qrcode/lv_qrcode.h"

#include <stdio.h>
#include <string.h>

static lv_obj_t * g_active_screen = NULL;

static void load_screen(lv_obj_t * screen) {
    if (screen == NULL) return;
    g_active_screen = screen;
    lv_scr_load(screen);
}

static lv_obj_t * create_provisioning_screen(const char * ssid) {
    lv_obj_t * screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0F1F27), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    const char * active_ssid = (ssid != NULL && ssid[0] != '\0') ? ssid : "PiSetup-XXXX";
    char wifi_qr_payload[128];
    snprintf(wifi_qr_payload, sizeof(wifi_qr_payload), "WIFI:T:nopass;S:%s;;", active_ssid);

    lv_obj_t * title = lv_label_create(screen);
    lv_label_set_text(title, "Device Setup Required");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE6F7FF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 36);

    lv_obj_t * line1 = lv_label_create(screen);
    lv_label_set_text(line1, "1. Open phone Wi-Fi settings");
    lv_obj_set_style_text_color(line1, lv_color_hex(0xB9D9E8), 0);
    lv_obj_set_style_text_font(line1, &lv_font_montserrat_14, 0);
    lv_obj_align(line1, LV_ALIGN_TOP_MID, 0, 110);

    lv_obj_t * line2 = lv_label_create(screen);
    lv_label_set_text_fmt(line2, "2. Scan this QR to join %s", active_ssid);
    lv_obj_set_style_text_color(line2, lv_color_hex(0x79D8FF), 0);
    lv_obj_set_style_text_font(line2, &lv_font_montserrat_14, 0);
    lv_obj_align(line2, LV_ALIGN_TOP_MID, 0, 140);

    lv_obj_t * qrcode = lv_qrcode_create(screen);
    lv_qrcode_set_size(qrcode, 180);
    lv_qrcode_set_dark_color(qrcode, lv_color_hex(0x0B1A20));
    lv_qrcode_set_light_color(qrcode, lv_color_hex(0xEAF8FF));
    lv_qrcode_update(qrcode, wifi_qr_payload, (uint32_t)strlen(wifi_qr_payload));
    lv_obj_align(qrcode, LV_ALIGN_TOP_MID, 0, 170);

    lv_obj_t * line3 = lv_label_create(screen);
    lv_label_set_text(line3, "3. In the mobile app, open Setup Device and submit home Wi-Fi.");
    lv_obj_set_width(line3, 730);
    lv_label_set_long_mode(line3, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(line3, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(line3, lv_color_hex(0xB9D9E8), 0);
    lv_obj_set_style_text_font(line3, &lv_font_montserrat_14, 0);
    lv_obj_align(line3, LV_ALIGN_TOP_MID, 0, 365);

    lv_obj_t * footer = lv_label_create(screen);
    lv_label_set_text(footer, "Waiting for credentials...");
    lv_obj_set_style_text_color(footer, lv_color_hex(0x8EA8B6), 0);
    lv_obj_set_style_text_font(footer, &lv_font_montserrat_14, 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -24);

    return screen;
}

void ui_init(void) {
    /* Initialize global styles first (like importing globals.css) */
    theme_init();

    /* Create the home screen and load it */
    /* lv_scr_load() = mounting your root component into the DOM   */
    load_screen(screen_home_create());
}

void ui_show_provisioning_screen(const char * ssid) {
    theme_init();
    load_screen(create_provisioning_screen(ssid));
}

void ui_navigate_focus_session(const char * title, uint32_t duration_seconds, bool is_quick_session, int task_id) {
    load_screen(screen_focus_session_create(title, duration_seconds, is_quick_session, task_id));
}

void ui_navigate_camera_preview(const char * title, uint32_t duration_seconds, bool is_quick_session, int task_id) {
    load_screen(screen_camera_preview_create(title, duration_seconds, is_quick_session, task_id));
}

void ui_navigate_home(void) {
    load_screen(screen_home_create());
}
