#include "device_config.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define DEVICE_CONFIG_PATH "device_config.json"
#define DEVICE_CONFIG_TMP_PATH "device_config.json.tmp"
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define DEVICE_CONFIG_DIR "/etc/deskclock"
#define DEVICE_CONFIG_PATH "/etc/deskclock/device_config.json"
#define DEVICE_CONFIG_TMP_PATH "/etc/deskclock/device_config.json.tmp"
#endif

#define DEVICE_CONFIG_DEFAULT_USER_ID 2

static DeviceConfig g_device_config;
static bool g_device_config_loaded = false;

static void copy_text_safe(char * dst, size_t dst_len, const char * src) {
    if (dst == NULL || dst_len == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

static bool json_get_string(const char * body, const char * key, char * out, size_t out_len) {
    if (body == NULL || key == NULL || out == NULL || out_len == 0) {
        return false;
    }

    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);

    const char * p = strstr(body, needle);
    if (p == NULL) {
        return false;
    }

    p = strchr(p, ':');
    if (p == NULL) {
        return false;
    }
    p++;

    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
        p++;
    }

    if (*p != '"') {
        return false;
    }

    p++;

    size_t idx = 0;
    while (*p != '\0' && *p != '"' && idx + 1 < out_len) {
        if (*p == '\\' && *(p + 1) != '\0') {
            p++;
        }

        out[idx++] = *p;
        p++;
    }

    out[idx] = '\0';
    return idx > 0;
}

static bool json_get_bool(const char * body, const char * key, bool * out) {
    if (body == NULL || key == NULL || out == NULL) {
        return false;
    }

    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);

    const char * p = strstr(body, needle);
    if (p == NULL) {
        return false;
    }

    p = strchr(p, ':');
    if (p == NULL) {
        return false;
    }
    p++;

    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
        p++;
    }

    if (strncmp(p, "true", 4) == 0) {
        *out = true;
        return true;
    }

    if (strncmp(p, "false", 5) == 0) {
        *out = false;
        return true;
    }

    return false;
}

static bool json_get_int(const char * body, const char * key, int * out) {
    if (body == NULL || key == NULL || out == NULL) {
        return false;
    }

    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);

    const char * p = strstr(body, needle);
    if (p == NULL) {
        return false;
    }

    p = strchr(p, ':');
    if (p == NULL) {
        return false;
    }
    p++;

    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
        p++;
    }

    int value = 0;
    if (sscanf(p, "%d", &value) != 1) {
        return false;
    }

    *out = value;
    return true;
}

void device_config_set_defaults(void) {
    memset(&g_device_config, 0, sizeof(g_device_config));
    g_device_config.user_id = DEVICE_CONFIG_DEFAULT_USER_ID;
    g_device_config.provisioned = false;
}

#ifndef _WIN32
static void ensure_config_dir(void) {
    struct stat st;
    if (stat(DEVICE_CONFIG_DIR, &st) == 0 && S_ISDIR(st.st_mode)) {
        return;
    }

    mkdir(DEVICE_CONFIG_DIR, 0755);
}
#endif

bool device_config_load(void) {
    if (g_device_config_loaded) {
        return true;
    }

    device_config_set_defaults();

    FILE * f = fopen(DEVICE_CONFIG_PATH, "rb");
    if (f == NULL) {
        g_device_config_loaded = true;
        return false;
    }

    char body[512];
    size_t n = fread(body, 1, sizeof(body) - 1, f);
    fclose(f);
    body[n] = '\0';

    (void)json_get_bool(body, "provisioned", &g_device_config.provisioned);
    (void)json_get_int(body, "userId", &g_device_config.user_id);
    (void)json_get_string(body, "wifiSsid", g_device_config.wifi_ssid, sizeof(g_device_config.wifi_ssid));
    (void)json_get_string(body, "wifiPassword", g_device_config.wifi_password, sizeof(g_device_config.wifi_password));

    g_device_config_loaded = true;
    return true;
}

bool device_config_save(const DeviceConfig * config) {
    if (config == NULL) {
        return false;
    }

#ifndef _WIN32
    ensure_config_dir();
#endif

    FILE * f = fopen(DEVICE_CONFIG_TMP_PATH, "wb");
    if (f == NULL) {
        return false;
    }

    int rc = fprintf(f,
                     "{\n"
                     "  \"provisioned\": %s,\n"
                     "  \"userId\": %d,\n"
                     "  \"wifiSsid\": \"%s\",\n"
                     "  \"wifiPassword\": \"%s\"\n"
                     "}\n",
                     config->provisioned ? "true" : "false",
                     config->user_id,
                     config->wifi_ssid,
                     config->wifi_password);

    if (rc <= 0) {
        fclose(f);
        remove(DEVICE_CONFIG_TMP_PATH);
        return false;
    }

    fflush(f);
#ifndef _WIN32
    fsync(fileno(f));
#endif
    fclose(f);

    if (rename(DEVICE_CONFIG_TMP_PATH, DEVICE_CONFIG_PATH) != 0) {
        remove(DEVICE_CONFIG_TMP_PATH);
        return false;
    }

#ifndef _WIN32
    chmod(DEVICE_CONFIG_PATH, 0600);
#endif

    g_device_config = *config;
    g_device_config_loaded = true;
    return true;
}

const DeviceConfig * device_config_get(void) {
    if (!g_device_config_loaded) {
        device_config_load();
    }

    return &g_device_config;
}

int device_config_get_user_id(void) {
    const DeviceConfig * config = device_config_get();
    return config->user_id;
}

bool device_config_is_provisioned(void) {
    const DeviceConfig * config = device_config_get();
    return config->provisioned;
}
