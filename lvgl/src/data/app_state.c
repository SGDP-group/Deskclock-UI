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
lv_obj_t * g_lbl_token       = NULL;

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

void app_state_set_tasks_loading(bool loading) {
    g_app_state.tasks_loading = loading;
}

void app_state_set_home_tasks(const HomeTask * tasks, uint8_t count) {
    uint8_t bounded_count = count;
    if (bounded_count > APP_MAX_HOME_TASKS) {
        bounded_count = APP_MAX_HOME_TASKS;
    }

    g_app_state.home_task_count = bounded_count;

    if (tasks == NULL || bounded_count == 0) {
        memset(g_app_state.home_tasks, 0, sizeof(g_app_state.home_tasks));
        return;
    }

    memset(g_app_state.home_tasks, 0, sizeof(g_app_state.home_tasks));
    memcpy(g_app_state.home_tasks, tasks, sizeof(HomeTask) * bounded_count);
}

void app_state_set_auth_token(const char * token) {
    if (token == NULL) {
        g_app_state.auth_token[0] = '\0';
    } else {
        strncpy(g_app_state.auth_token, token, sizeof(g_app_state.auth_token) - 1);
        g_app_state.auth_token[sizeof(g_app_state.auth_token) - 1] = '\0';
    }

    if (g_lbl_token != NULL) {
        if (g_app_state.auth_token[0] != '\0') {
            lv_label_set_text_fmt(g_lbl_token, "Token: %s", g_app_state.auth_token);
            lv_obj_clear_flag(g_lbl_token, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_lbl_token, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
