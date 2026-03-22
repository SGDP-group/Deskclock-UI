#ifndef PAIRING_H
#define PAIRING_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    PAIRING_SUCCESS = 0,
    PAIRING_EXPIRED = 1,
    PAIRING_ERROR   = 2
} PairingResult;

/* Starts a new pairing session; writes session id string into out_session_id (buffer of at least 64). */
bool pairing_session_create(char * out_session_id, size_t out_len);

/* Polls server for status; blocks until paired/expired/error. Writes user id on success. */
PairingResult pairing_session_poll(const char * session_id, char * out_user_id, size_t out_len);

/* Render the QR data as a QR code onto the active LVGL screen. */
bool qr_render_lvgl(const char * qr_data);

/* Bootstraps the pairing flow on the current screen. */
void pairing_screen_start(void);

/* Called when pairing succeeds; app can provide this elsewhere. */
void on_pairing_complete(const char * user_id);

#endif /* PAIRING_H */
