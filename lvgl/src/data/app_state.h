#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include "lvgl/lvgl.h"

/**
 * app_state.h — Global application state
 * React analogy: your Redux store or a top-level React Context
 *
 * Any file can #include this and read/write state via the setter functions.
 * Setters automatically update any registered UI elements.
 */

/* -----------------------------------------------------------------------
 * State struct — like your Redux state shape
 * Add fields here as your app grows
 * ----------------------------------------------------------------------- */
typedef struct {
    int      id;
    bool     completed;
    int      duration_minutes;
    char     title[64];
    char     subtitle[96];
    char     time_range[24];
    char     status[20];
} HomeTask;

#define APP_MAX_HOME_TASKS 12

typedef struct {
    float    temperature;
    float    humidity;
    int      wifi_rssi;
    bool     is_loading;
    char     status_message[64];
    bool     tasks_loading;
    uint8_t  home_task_count;
    HomeTask home_tasks[APP_MAX_HOME_TASKS];
} AppState;

/* Global instance — like createContext() + a default value */
extern AppState g_app_state;

/* -----------------------------------------------------------------------
 * UI element references — like useRef()
 * Set these pointers in your screen files after creating the widgets.
 * app_state setters will update them automatically.
 * ----------------------------------------------------------------------- */
extern lv_obj_t * g_lbl_status;
extern lv_obj_t * g_lbl_temperature;
extern lv_obj_t * g_spinner;

/* -----------------------------------------------------------------------
 * Setters — like dispatch(action) in Redux, or setState in React
 * Each one updates the state value AND refreshes the relevant UI widget
 * ----------------------------------------------------------------------- */
void app_state_set_status(const char * msg);
void app_state_set_temperature(float temp);
void app_state_set_loading(bool loading);
void app_state_set_tasks_loading(bool loading);
void app_state_set_home_tasks(const HomeTask * tasks, uint8_t count);

#endif /* APP_STATE_H */
