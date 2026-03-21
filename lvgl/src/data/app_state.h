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
    float    temperature;
    float    humidity;
    int      wifi_rssi;
    bool     is_loading;
    char     status_message[64];
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

#endif /* APP_STATE_H */
