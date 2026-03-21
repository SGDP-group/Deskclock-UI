/**
 * app_state.c — Global application state
 * React analogy: your Redux store / top-level Context
 *
 * How to use:
 *   1. In your screen file, set the pointer after creating the widget:
 *        g_lbl_temperature = lv_label_create(screen);
 *
 *   2. From anywhere in the app, update state and the UI updates automatically:
 *        app_state_set_temperature(23.5f);
 */

#include "app_state.h"
#include <string.h>
#include <stdio.h>

/* -----------------------------------------------------------------------
 * Global state instance — zero-initialized by default
 * ----------------------------------------------------------------------- */
AppState g_app_state = {0};

/* -----------------------------------------------------------------------
 * UI references — NULL until a screen registers them
 * ----------------------------------------------------------------------- */
lv_obj_t * g_lbl_status      = NULL;
lv_obj_t * g_lbl_temperature = NULL;
lv_obj_t * g_spinner         = NULL;

/* -----------------------------------------------------------------------
 * Setters
 * ----------------------------------------------------------------------- */

void app_state_set_status(const char * msg) {
    strncpy(g_app_state.status_message, msg, sizeof(g_app_state.status_message) - 1);
    g_app_state.status_message[sizeof(g_app_state.status_message) - 1] = '\0';

    if (g_lbl_status != NULL) {
        lv_label_set_text(g_lbl_status, g_app_state.status_message);
    }
}

void app_state_set_temperature(float temp) {
    g_app_state.temperature = temp;

    if (g_lbl_temperature != NULL) {
        lv_label_set_text_fmt(g_lbl_temperature, "%.1f C", temp);
    }
}

void app_state_set_loading(bool loading) {
    g_app_state.is_loading = loading;

    if (g_spinner != NULL) {
        if (loading) {
            lv_obj_clear_flag(g_spinner, LV_OBJ_FLAG_HIDDEN); /* show */
        } else {
            lv_obj_add_flag(g_spinner, LV_OBJ_FLAG_HIDDEN);   /* hide */
        }
    }
}
