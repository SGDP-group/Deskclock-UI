/**
 * ui.c — Root UI initializer
 * React analogy: this is your App.jsx
 *
 * All screen creation and initial navigation happens here.
 */

#include "ui.h"
#include "styles/theme.h"
#include "screens/screen_home.h"
#include "screens/screen_token.h"
#include "src/home_config.h"
#include "src/config_io.h"

/* Static screen references for transitions */
static lv_obj_t * g_current_home_screen = NULL;

void ui_transition_to_home(void) {
    if (g_current_home_screen == NULL) {
        g_current_home_screen = screen_home_create();
    }
    lv_scr_load(g_current_home_screen);
}

void ui_init(void) {
    /* Initialize global styles first (like importing globals.css) */
    theme_init();

    /* Try to load pairing from disk */
    int user_id = 0;
    char token[128] = {0};
    bool has_saved_pairing = config_load_pairing(&user_id, token, sizeof(token));

    /* Choose screen based on whether we have a saved user_id. */
    if (has_saved_pairing && user_id > 0) {
        /* User was previously paired; load home screen directly */
        g_current_home_screen = screen_home_create();
        lv_scr_load(g_current_home_screen);
    } else {
        /* No pairing found; show token screen */
        lv_obj_t * token_screen = screen_token_create();
        lv_scr_load(token_screen);
    }
}
