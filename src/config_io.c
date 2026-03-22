#include "config_io.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <pwd.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

#define CONFIG_FILENAME "deskclock_config.json"

static char g_config_dir[512] = {0};
static bool g_config_dir_initialized = false;

static const char * get_config_dir_internal(void) {
    if (g_config_dir_initialized) {
        return g_config_dir;
    }

#ifdef _WIN32
    /* Windows: use %APPDATA%\Deskclock */
    CHAR appdata_path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata_path))) {
        snprintf(g_config_dir, sizeof(g_config_dir), "%s\\Deskclock", appdata_path);
        CreateDirectoryA(g_config_dir, NULL);
    } else {
        strcpy(g_config_dir, ".\\deskclock_config");
        CreateDirectoryA(g_config_dir, NULL);
    }
#else
    /* POSIX: use ~/.deskclock */
    const char * home = getenv("HOME");
    if (home == NULL) {
        struct passwd * pw = getpwuid(getuid());
        if (pw != NULL) {
            home = pw->pw_dir;
        }
    }

    if (home != NULL) {
        snprintf(g_config_dir, sizeof(g_config_dir), "%s/.deskclock", home);
        mkdir(g_config_dir, 0755);
    } else {
        strcpy(g_config_dir, "./.deskclock");
        mkdir(g_config_dir, 0755);
    }
#endif

    g_config_dir_initialized = true;
    return g_config_dir;
}

static bool json_get_int(const char * obj, const char * key, int * out) {
    if (obj == NULL || key == NULL || out == NULL) {
        return false;
    }

    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char * p = strstr(obj, needle);
    if (p == NULL) {
        return false;
    }

    p = strchr(p, ':');
    if (p == NULL) {
        return false;
    }
    p++;

    while (*p && (*p == ' ' || *p == '\t')) {
        p++;
    }

    *out = strtol(p, NULL, 10);
    return true;
}

static bool json_get_string(const char * obj, const char * key, char * out, size_t out_len) {
    if (obj == NULL || key == NULL || out == NULL || out_len == 0) {
        return false;
    }

    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char * p = strstr(obj, needle);
    if (p == NULL) {
        return false;
    }

    p = strchr(p, ':');
    if (p == NULL) {
        return false;
    }
    p++;

    while (*p && (*p == ' ' || *p == '\t' || *p == ':')) {
        p++;
    }

    if (*p == '"') {
        p++;
    }

    size_t i = 0;
    while (i < out_len - 1 && *p && *p != '"' && *p != ',') {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}

const char * config_get_dir(void) {
    return get_config_dir_internal();
}

bool config_load_pairing(int * out_user_id, char * out_token, size_t token_len) {
    if (out_user_id == NULL || out_token == NULL || token_len == 0) {
        return false;
    }

    const char * config_dir = get_config_dir_internal();
    char config_path[512];
    snprintf(config_path, sizeof(config_path), "%s/%s", config_dir, CONFIG_FILENAME);

    FILE * file = fopen(config_path, "r");
    if (file == NULL) {
        return false; /* Config file doesn't exist yet */
    }

    /* Read entire file */
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0 || file_size > 16384) {
        fclose(file);
        return false;
    }

    char * buffer = (char *)malloc(file_size + 1);
    if (buffer == NULL) {
        fclose(file);
        return false;
    }

    size_t read_bytes = fread(buffer, 1, file_size, file);
    fclose(file);

    if (read_bytes != (size_t)file_size) {
        free(buffer);
        return false;
    }

    buffer[file_size] = '\0';

    /* Parse JSON */
    bool success = json_get_int(buffer, "userId", out_user_id) &&
                   json_get_string(buffer, "token", out_token, token_len);

    free(buffer);
    return success;
}

bool config_save_pairing(int user_id, const char * token) {
    if (token == NULL) {
        return false;
    }

    const char * config_dir = get_config_dir_internal();
    char config_path[512];
    snprintf(config_path, sizeof(config_path), "%s/%s", config_dir, CONFIG_FILENAME);

    FILE * file = fopen(config_path, "w");
    if (file == NULL) {
        fprintf(stderr, "Failed to open config file for writing: %s\n", config_path);
        return false;
    }

    /* Write JSON config */
    fprintf(file, "{\n");
    fprintf(file, "  \"userId\": %d,\n", user_id);
    fprintf(file, "  \"token\": \"%s\"\n", token);
    fprintf(file, "}\n");

    fclose(file);
    printf("Config saved to %s\n", config_path);
    return true;
}

bool config_clear_pairing(void) {
    const char * config_dir = get_config_dir_internal();
    char config_path[512];
    snprintf(config_path, sizeof(config_path), "%s/%s", config_dir, CONFIG_FILENAME);

    if (remove(config_path) != 0 && errno != ENOENT) {
        fprintf(stderr, "Failed to remove config file\n");
        return false;
    }

    return true;
}
