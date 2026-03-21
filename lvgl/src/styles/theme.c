/**
 * theme.c — Global style definitions
 * React analogy: styles/globals.css
 *
 * Define your lv_style_t variables here (like CSS classes),
 * then expose helper functions to apply them to any object.
 *
 * Usage:
 *   theme_init();               // call once in ui_init()
 *   apply_card_style(my_panel); // like className="card"
 */

#include "theme.h"

/* -----------------------------------------------------------------------
 * Style variables — like CSS class definitions
 * Declared static so they live for the entire program lifetime
 * ----------------------------------------------------------------------- */
static lv_style_t style_card;
static lv_style_t style_primary_btn;
static lv_style_t style_primary_btn_pressed;
static lv_style_t style_title_text;

/* -----------------------------------------------------------------------
 * theme_init — call once at startup
 * ----------------------------------------------------------------------- */
void theme_init(void) {

    /* ------------------------------------------------------------------
     * Card style — like CSS .card
     * ------------------------------------------------------------------ */
    lv_style_init(&style_card);
    lv_style_set_radius(&style_card, 10);
    lv_style_set_bg_color(&style_card, lv_color_hex(0x16213E));
    lv_style_set_bg_opa(&style_card, LV_OPA_COVER);
    lv_style_set_border_width(&style_card, 1);
    lv_style_set_border_color(&style_card, lv_color_hex(0x0F3460));
    lv_style_set_pad_all(&style_card, 14);
    lv_style_set_shadow_width(&style_card, 12);
    lv_style_set_shadow_ofs_y(&style_card, 5);
    lv_style_set_shadow_color(&style_card, lv_color_hex(0x000000));
    lv_style_set_shadow_opa(&style_card, LV_OPA_30);

    /* ------------------------------------------------------------------
     * Primary button — default state
     * ------------------------------------------------------------------ */
    lv_style_init(&style_primary_btn);
    lv_style_set_bg_color(&style_primary_btn, lv_color_hex(0xE94560));
    lv_style_set_bg_opa(&style_primary_btn, LV_OPA_COVER);
    lv_style_set_radius(&style_primary_btn, 8);
    lv_style_set_border_width(&style_primary_btn, 0);
    lv_style_set_text_color(&style_primary_btn, lv_color_hex(0xFFFFFF));
    lv_style_set_shadow_width(&style_primary_btn, 8);
    lv_style_set_shadow_ofs_y(&style_primary_btn, 4);
    lv_style_set_shadow_color(&style_primary_btn, lv_color_hex(0xE94560));
    lv_style_set_shadow_opa(&style_primary_btn, LV_OPA_40);

    /* ------------------------------------------------------------------
     * Primary button — pressed state (like CSS :active)
     * ------------------------------------------------------------------ */
    lv_style_init(&style_primary_btn_pressed);
    lv_style_set_bg_color(&style_primary_btn_pressed, lv_color_hex(0xC73652));
    lv_style_set_shadow_opa(&style_primary_btn_pressed, LV_OPA_20);

    /* ------------------------------------------------------------------
     * Title text style
     * ------------------------------------------------------------------ */
    lv_style_init(&style_title_text);
    lv_style_set_text_font(&style_title_text, &lv_font_montserrat_24);
    lv_style_set_text_color(&style_title_text, lv_color_hex(0xFFFFFF));
    lv_style_set_text_letter_space(&style_title_text, 1);
}

/* -----------------------------------------------------------------------
 * Style appliers — like adding a CSS class to a DOM element
 * ----------------------------------------------------------------------- */

void apply_card_style(lv_obj_t * obj) {
    lv_obj_add_style(obj, &style_card, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void apply_primary_btn_style(lv_obj_t * obj) {
    lv_obj_add_style(obj, &style_primary_btn,         LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(obj, &style_primary_btn_pressed, LV_PART_MAIN | LV_STATE_PRESSED);
}

void apply_title_text_style(lv_obj_t * obj) {
    lv_obj_add_style(obj, &style_title_text, LV_PART_MAIN | LV_STATE_DEFAULT);
}
