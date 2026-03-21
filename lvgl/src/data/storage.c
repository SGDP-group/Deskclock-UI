/**
 * storage.c — Simple key-value persistent storage
 * Like localStorage for your LVGL app
 *
 * Usage:
 *   storage_set("brightness", "80");
 *
 *   char val[16];
 *   if (storage_get("brightness", val, sizeof(val))) {
 *       int brightness = atoi(val);
 *   }
 */

#include "storage.h"
#include <stdio.h>
#include <string.h>

/* File path differs between Windows (dev) and Pi (production) */
#ifdef _WIN32
    #define STORAGE_FILE "app_data.conf"
#else
    #define STORAGE_FILE "/home/pi/app_data.conf"
#endif

void storage_set(const char * key, const char * value) {
    /* Append-only write — last occurrence wins on read */
    FILE * f = fopen(STORAGE_FILE, "a");
    if (f) {
        fprintf(f, "%s=%s\n", key, value);
        fclose(f);
    }
}

int storage_get(const char * key, char * out_value, size_t max_len) {
    FILE * f = fopen(STORAGE_FILE, "r");
    if (!f) return 0;

    char line[256];
    int found = 0;

    while (fgets(line, sizeof(line), f)) {
        char * eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0'; /* Split at '=' */

        if (strcmp(line, key) == 0) {
            char * val = eq + 1;
            size_t len = strlen(val);

            /* Trim trailing newline */
            if (len > 0 && val[len - 1] == '\n') val[len - 1] = '\0';

            strncpy(out_value, val, max_len - 1);
            out_value[max_len - 1] = '\0';
            found = 1;
            /* Don't break — last occurrence wins (update semantics) */
        }
    }

    fclose(f);
    return found;
}
