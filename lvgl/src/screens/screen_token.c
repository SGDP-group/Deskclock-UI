/**
 * screen_token.c — Minimal screen to show the generated auth token.
 */

#include "screen_token.h"
#include "lvgl/lvgl.h"
#include "../data/app_state.h"
#include "src/home_api_client.h"
#include "src/home_config.h"

static void fetch_and_render_token(lv_obj_t * token_label) {
    if (token_label == NULL) {
        return;
    }

    char token[128] = {0};
    bool ok = home_api_fetch_auth_token(token, sizeof(token));
    if (ok && token[0] != '\0') {
        app_state_set_auth_token(token);
    } else {
        lv_label_set_text(token_label, "Token unavailable");
    }
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

    lv_obj_t * title = lv_label_create(container);
    lv_label_set_text(title, "Here is your token");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);

    lv_obj_t * token_lbl = lv_label_create(container);
    lv_obj_set_style_text_font(token_lbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(token_lbl, lv_color_white(), LV_PART_MAIN);
    lv_label_set_text(token_lbl, "Loading...");

    /* Expose to state setters so future updates keep the UI in sync. */
    g_lbl_token = token_lbl;

    fetch_and_render_token(token_lbl);

    return screen;
}
