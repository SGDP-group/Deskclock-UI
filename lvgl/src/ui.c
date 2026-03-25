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

static lv_obj_t * g_active_screen = NULL;

static void load_screen(lv_obj_t * screen) {
    if (screen == NULL) return;
    g_active_screen = screen;
    lv_scr_load(screen);
}

void ui_init(void) {
    /* Initialize global styles first (like importing globals.css) */
    theme_init();

    /* Create the home screen and load it */
    /* lv_scr_load() = mounting your root component into the DOM   */
    load_screen(screen_home_create());
}

void ui_navigate_focus_session(const char * title, uint32_t duration_seconds, bool is_quick_session, int task_id) {
    load_screen(screen_focus_session_create(title, duration_seconds, is_quick_session, task_id));
}

void ui_navigate_home(void) {
    load_screen(screen_home_create());
}
