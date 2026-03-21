#ifndef STORAGE_H
#define STORAGE_H

#include <stddef.h>

/**
 * storage.h — Simple key-value persistent storage
 * Like localStorage for your LVGL app
 *
 * Stores key=value pairs in a plain text file on disk.
 * Works on both Windows (dev) and Pi (production) via the DATA_FILE path.
 */

/* Save a value — creates or appends to the storage file */
void storage_set(const char * key, const char * value);

/* Read a value — returns 1 if found, 0 if not found */
/* out_value is filled with the stored string          */
int  storage_get(const char * key, char * out_value, size_t max_len);

#endif /* STORAGE_H */
