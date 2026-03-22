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
#include <stdio.h>

/* Static screen references for transitions */
static lv_obj_t * g_current_home_screen = NULL;

void ui_transition_to_home(void) {
    printf("[UI] ui_transition_to_home() called - transitioning from token screen to home\n");
    if (g_current_home_screen == NULL) {
        printf("[UI] Creating home screen...\n");
        g_current_home_screen = screen_home_create();
    }
    printf("[UI] Loading home screen\n");
    lv_scr_load(g_current_home_screen);
    printf("[UI] Home screen loaded\n");
}

void ui_init(void) {
    printf("[UI] Initializing UI...\n");
    /* Initialize global styles first (like importing globals.css) */
    theme_init();

    /* Try to load pairing from disk */
    int user_id = 0;
    char token[128] = {0};
    bool has_saved_pairing = config_load_pairing(&user_id, token, sizeof(token));
    printf("[UI] config_load_pairing() returned: has_saved_pairing=%s, user_id=%d\n", 
           has_saved_pairing ? "true" : "false", user_id);

    /* Choose screen based on whether we have a saved user_id. */
    if (has_saved_pairing && user_id > 0) {
        /* User was previously paired; load home screen directly */
        printf("[UI] FOUND pairing on disk (user_id=%d). Loading home screen.\n", user_id);
        g_current_home_screen = screen_home_create();
        lv_scr_load(g_current_home_screen);
    } else {
        /* No pairing found; show token screen */
        printf("[UI] No pairing found. Loading token screen for pairing...\n");
        lv_obj_t * token_screen = screen_token_create();
        lv_scr_load(token_screen);
    }
    printf("[UI] UI initialization complete\n");
}
