#ifndef THEME_H
#define THEME_H

#include "lvgl/lvgl.h"

/**
 * theme.h — Global style definitions
 * React analogy: styles/globals.css + a set of CSS classes
 */

/* Call once at startup in ui_init() */
void theme_init(void);

/* Apply pre-built styles to any object — like adding a CSS class */
void apply_card_style(lv_obj_t * obj);
void apply_primary_btn_style(lv_obj_t * obj);
void apply_title_text_style(lv_obj_t * obj);

#endif /* THEME_H */
