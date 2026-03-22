#ifndef CALLBACK_SERVER_H
#define CALLBACK_SERVER_H

#include <stdbool.h>
#include <stddef.h>

/**
 * Callback Server — Listens for POST /pairing/callback with userId and token
 * Runs in background thread; thread-safe
 */

/* Start the callback server; binds to 127.0.0.1:9000 
 * Must call this once during pairing setup
 * Returns false if port binding fails (e.g., already in use)
 */
bool callback_server_start(const char * session_id);

/* Stop the callback server gracefully */
void callback_server_stop(void);

/* Check if server is currently running */
bool callback_server_is_running(void);

/* Get the last received pairing data (userId and token) 
 * Call this after on_pairing_complete is invoked to retrieve the values
 */
void callback_server_get_last_pairing(int * out_user_id, char * out_token, size_t token_len);

#endif /* CALLBACK_SERVER_H */
