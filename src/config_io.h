#ifndef CONFIG_IO_H
#define CONFIG_IO_H

#include <stdbool.h>
#include <stddef.h>

/**
 * Config I/O — Disk persistence for userId and auth token
 * Handles Windows and POSIX file paths
 */

/* Load userId and token from disk
 * Returns true if pairing was found on disk, false otherwise
 * Outputs are only valid if function returns true
 */
bool config_load_pairing(int * out_user_id, char * out_token, size_t token_len);

/* Save userId and token to disk */
bool config_save_pairing(int user_id, const char * token);

/* Clear pairing data from disk (factory reset) */
bool config_clear_pairing(void);

/* Get the config directory path (for debugging/testing) */
const char * config_get_dir(void);

#endif /* CONFIG_IO_H */
