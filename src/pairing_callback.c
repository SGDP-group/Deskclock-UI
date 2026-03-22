#include "pairing.h"
#include "config_io.h"
#include "callback_server.h"
#include "lvgl/lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declaration (defined in app_state.h) */
extern void app_state_set_auth_token(const char * token);
extern void app_state_set_pairing(int user_id, const char * token);

/* Forward for screen transition */
extern void ui_transition_to_home(void);

/* Wrapper function for lv_async_call compatibility */
static void transition_async_cb(void * arg) {
    (void)arg;
    ui_transition_to_home();
}

/**
 * on_pairing_complete() — Weak function override
 * Called when pairing completes (from callback_server.c)
 * Saves pairing data to disk and transitions to home screen
 */
void on_pairing_complete(const char * user_id_str) {
    printf("[PAIRING_CB] *** on_pairing_complete() called ***\n");
    printf("[PAIRING_CB] user_id_str=%s\n", user_id_str ? user_id_str : "NULL");
    
    if (user_id_str == NULL) {
        printf("[PAIRING_CB] ERROR: user_id_str is NULL\n");
        fprintf(stderr, "on_pairing_complete: user_id_str is NULL\n");
        return;
    }

    int user_id = atoi(user_id_str);
    if (user_id <= 0) {
        printf("[PAIRING_CB] ERROR: invalid user_id: %s\n", user_id_str);
        fprintf(stderr, "on_pairing_complete: invalid user_id: %s\n", user_id_str);
        return;
    }

    /* Retrieve the token that was stored by callback_server before calling this function */
    char token[128] = {0};
    callback_server_get_last_pairing(NULL, token, sizeof(token));

    printf("[PAIRING_CB] Pairing complete! user_id=%d, token=%s\n", user_id, token);

    /* Save pairing data to disk */
    if (config_save_pairing(user_id, token)) {
        printf("[PAIRING_CB] ✓ Pairing data saved to disk\n");
    } else {
        printf("[PAIRING_CB] ERROR: Failed to save pairing to disk\n");
        fprintf(stderr, "Failed to save pairing to disk\n");
    }

    /* Update app state with user_id and token */
    printf("[PAIRING_CB] Updating app state with user_id=%d and token=%s\n", user_id, token);
    app_state_set_pairing(user_id, token);

    /* Transition to home screen
     * Use lv_async_call to ensure thread safety when called from callback server thread
     */
    printf("[PAIRING_CB] Transitioning to home screen...\n");
    lv_async_call(transition_async_cb, NULL);

    printf("[PAIRING_CB] ✓ Pairing callback complete\n");
}
