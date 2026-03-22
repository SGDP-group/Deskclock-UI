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

void ui_init(void) {
    /* Initialize global styles first (like importing globals.css) */
    theme_init();

    /* Choose screen based on whether we're in token-only mode. */
    if (HOME_API_USER_ID <= 0) {
        lv_obj_t * token_screen = screen_token_create();
        lv_scr_load(token_screen);
    } else {
        /* Create the home screen and load it */
        /* lv_scr_load() = mounting your root component into the DOM   */
        lv_obj_t * home = screen_home_create();
        lv_scr_load(home);
    }
}
