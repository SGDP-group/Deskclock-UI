/**
 * screen_token.c — Show QR for generated auth token.
 */

#include "screen_token.h"
#include "lvgl/lvgl.h"
#include "../data/app_state.h"
#include "../libs/qrcode/lv_qrcode.h"
#include "src/home_api_client.h"
#include "src/home_config.h"
#include "src/pairing.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t * g_status_lbl = NULL;
static lv_obj_t * g_token_lbl = NULL;
static lv_obj_t * g_qr_container = NULL;
static lv_obj_t * g_qr_obj = NULL;

/* Render a QR code for the given payload inside the provided parent. */
static bool render_qr(lv_obj_t * parent, const char * data) {
    if (parent == NULL || data == NULL) {
        return false;
    }

    if (g_qr_obj == NULL) {
        g_qr_obj = lv_qrcode_create(parent);
        lv_qrcode_set_size(g_qr_obj, 220);
        lv_qrcode_set_dark_color(g_qr_obj, lv_color_black());
        lv_qrcode_set_light_color(g_qr_obj, lv_color_white());
    }

    if (lv_qrcode_update(g_qr_obj, data, (uint32_t)strlen(data)) != LV_RESULT_OK) {
        return false;
    }

    return true;
}

static void fetch_and_render_token(lv_obj_t * qr_container) {
    if (qr_container == NULL) {
        return;
    }

    lv_obj_clean(qr_container);
    g_qr_obj = NULL;

    if (g_token_lbl != NULL) {
        lv_label_set_text(g_token_lbl, "");
    }

#if HOME_API_USER_ID <= 0
    /* Pairing mode: always request a fresh token from authToken/generate. */
    char token[128] = {0};
    bool ok = home_api_fetch_auth_token(token, sizeof(token));
    if (ok && token[0] != '\0') {
        app_state_set_auth_token(token);
        if (!render_qr(qr_container, token)) {
            lv_label_set_text(g_status_lbl, "QR render failed");
        } else {
            lv_label_set_text(g_status_lbl, "");
            if (g_token_lbl != NULL) {
                lv_label_set_text(g_token_lbl, token);
            }
        }
    } else {
        lv_label_set_text(g_status_lbl, "Token unavailable");
    }
#else
    /* Non-pairing mode: use an already provided token if available. */
    if (g_app_state.auth_token[0] != '\0') {
        if (!render_qr(qr_container, g_app_state.auth_token)) {
            lv_label_set_text(g_status_lbl, "QR render failed");
        } else {
            lv_label_set_text(g_status_lbl, "");
            if (g_token_lbl != NULL) {
                lv_label_set_text(g_token_lbl, g_app_state.auth_token);
            }
        }
    } else {
        lv_label_set_text(g_status_lbl, "Token unavailable");
    }
#endif
}

lv_obj_t * screen_token_create(void) {
    lv_obj_t * screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * container = lv_obj_create(screen);
    lv_obj_remove_style_all(container);
    lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    lv_obj_center(container);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(container, 18, LV_PART_MAIN);

    lv_obj_t * title = lv_label_create(container);
    lv_label_set_text(title, "Scan QR to pair");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);

    g_status_lbl = lv_label_create(container);
    lv_obj_set_style_text_font(g_status_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_status_lbl, lv_color_hex(0xD8DEE9), LV_PART_MAIN);
    lv_label_set_text(g_status_lbl, "Loading...");

    g_qr_container = lv_obj_create(container);
    lv_obj_remove_style_all(g_qr_container);
    lv_obj_set_size(g_qr_container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    g_token_lbl = lv_label_create(container);
    lv_obj_set_width(g_token_lbl, lv_pct(90));
    lv_label_set_long_mode(g_token_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(g_token_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(g_token_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_token_lbl, lv_color_white(), LV_PART_MAIN);
    lv_label_set_text(g_token_lbl, "");

    fetch_and_render_token(g_qr_container);

    printf("[SCREEN_TOKEN] Token screen created. Starting pairing session...\n");
    pairing_screen_start();

    return screen;
}
