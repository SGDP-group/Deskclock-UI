#include "doormount_service.h"

#include "device_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <arpa/inet.h>
#include <ctype.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#define DOORMOUNT_AP_PREFIX "DoorMount-"
#define DOORMOUNT_DEVICE_HOST "192.168.4.1"
#define DOORMOUNT_DEVICE_PORT 8080
#define DOORMOUNT_SETUP_PATH "/api/doormount/setup"
#define DOORMOUNT_WIFI_IFNAME "wlan0"
#define DOORMOUNT_SCAN_CMD "nmcli -t -f SSID dev wifi list --rescan yes"
#define DOORMOUNT_LOG_PATH "doormount_log.txt"

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

static void set_error(char * error, size_t error_len, const char * message) {
    if (error == NULL || error_len == 0) {
        return;
    }

    copy_text_safe(error, error_len, message);
}

static void trim_ascii(char * text) {
    if (text == NULL || text[0] == '\0') {
        return;
    }

    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == '\r' || text[len - 1] == '\n' || text[len - 1] == ' ' || text[len - 1] == '\t')) {
        text[len - 1] = '\0';
        len--;
    }

#ifndef _WIN32
    size_t start = 0;
    while (text[start] != '\0' && isspace((unsigned char)text[start])) {
        start++;
    }

    if (start > 0) {
        memmove(text, text + start, strlen(text + start) + 1);
    }
#endif
}

static bool starts_with(const char * text, const char * prefix) {
    if (text == NULL || prefix == NULL) {
        return false;
    }

    size_t prefix_len = strlen(prefix);
    return strncmp(text, prefix, prefix_len) == 0;
}

static bool contains_unsafe_shell_chars(const char * value) {
    if (value == NULL) {
        return true;
    }

    for (const char * p = value; *p != '\0'; ++p) {
        if (*p == '"' || *p == '\'' || *p == ';' || *p == '`' || *p == '$' || *p == '\\') {
            return true;
        }
    }

    return false;
}

static void log_doormount(const char * message) {
    FILE * f = fopen(DOORMOUNT_LOG_PATH, "a");
    if (f == NULL) {
        return;
    }

    time_t now = time(NULL);
    struct tm tm_info;
    memset(&tm_info, 0, sizeof(tm_info));
#ifdef _WIN32
    if (localtime_s(&tm_info, &now) != 0) {
        struct tm * fallback = localtime(&now);
        if (fallback != NULL) {
            tm_info = *fallback;
        }
    }
#else
    if (localtime_r(&now, &tm_info) == NULL) {
        struct tm * fallback = localtime(&now);
        if (fallback != NULL) {
            tm_info = *fallback;
        }
    }
#endif

    char time_buf[32] = {0};
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);

    fprintf(f, "[%s] %s\n", time_buf, (message != NULL) ? message : "(null)");
    fclose(f);
}

static int run_command(const char * cmd) {
    if (cmd == NULL || cmd[0] == '\0') {
        log_doormount("run_command called with empty command");
        return -1;
    }

    int rc = system(cmd);

    char log_line[512];
    snprintf(log_line, sizeof(log_line), "cmd rc=%d :: %s", rc, cmd);
    log_doormount(log_line);

    return rc;
}

#ifndef _WIN32
static bool list_contains_ssid(const DoormountNetworkList * list, const char * ssid) {
    if (list == NULL || ssid == NULL) {
        return false;
    }

    for (uint8_t i = 0; i < list->count; i++) {
        if (strcmp(list->ssids[i], ssid) == 0) {
            return true;
        }
    }

    return false;
}

static bool connect_open_wifi(const char * ssid) {
    if (ssid == NULL || ssid[0] == '\0') {
        return false;
    }

    if (contains_unsafe_shell_chars(ssid)) {
        return false;
    }

    char cmd[256];
    snprintf(cmd,
             sizeof(cmd),
             "nmcli dev wifi connect \"%s\" ifname " DOORMOUNT_WIFI_IFNAME,
             ssid);

    return run_command(cmd) == 0;
}

static bool connect_secured_wifi(const char * ssid, const char * password) {
    if (ssid == NULL || password == NULL || ssid[0] == '\0' || password[0] == '\0') {
        return false;
    }

    if (contains_unsafe_shell_chars(ssid) || contains_unsafe_shell_chars(password)) {
        return false;
    }

    char cmd[320];
    snprintf(cmd,
             sizeof(cmd),
             "nmcli dev wifi connect \"%s\" password \"%s\" ifname " DOORMOUNT_WIFI_IFNAME,
             ssid,
             password);

    return run_command(cmd) == 0;
}

static bool send_all(int sock, const char * data, size_t data_len) {
    size_t sent = 0;
    while (sent < data_len) {
        ssize_t n = send(sock, data + sent, data_len - sent, 0);
        if (n <= 0) {
            return false;
        }

        sent += (size_t)n;
    }

    return true;
}

static bool response_is_success(const char * response) {
    if (response == NULL) {
        return false;
    }

    return (strncmp(response, "HTTP/1.1 2", 10) == 0) ||
           (strncmp(response, "HTTP/1.0 2", 10) == 0);
}

static bool post_doormount_setup(const DeviceConfig * config, char * error, size_t error_len) {
    if (config == NULL) {
        set_error(error, error_len, "Invalid device config");
        return false;
    }

    if (contains_unsafe_shell_chars(config->wifi_ssid) || contains_unsafe_shell_chars(config->wifi_password)) {
        set_error(error, error_len, "Saved Wi-Fi contains unsupported characters");
        return false;
    }

    char body[256];
    int body_len = snprintf(body,
                            sizeof(body),
                            "{\"wifiSsid\":\"%s\",\"wifiPassword\":\"%s\",\"userId\":%d}",
                            config->wifi_ssid,
                            config->wifi_password,
                            config->user_id);
    if (body_len <= 0 || (size_t)body_len >= sizeof(body)) {
        set_error(error, error_len, "Doormount payload too large");
        return false;
    }

    char request[1024];
    int request_len = snprintf(request,
                               sizeof(request),
                               "POST " DOORMOUNT_SETUP_PATH " HTTP/1.1\r\n"
                               "Host: " DOORMOUNT_DEVICE_HOST "\r\n"
                               "Connection: close\r\n"
                               "Content-Type: application/json\r\n"
                               "Content-Length: %d\r\n\r\n"
                               "%s",
                               body_len,
                               body);
    if (request_len <= 0 || (size_t)request_len >= sizeof(request)) {
        set_error(error, error_len, "Doormount request too large");
        return false;
    }

    struct addrinfo hints;
    struct addrinfo * res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port[8];
    snprintf(port, sizeof(port), "%d", DOORMOUNT_DEVICE_PORT);

    if (getaddrinfo(DOORMOUNT_DEVICE_HOST, port, &hints, &res) != 0 || res == NULL) {
        set_error(error, error_len, "Could not resolve DoorMount endpoint");
        return false;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        set_error(error, error_len, "Could not open DoorMount socket");
        return false;
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        close(sock);
        freeaddrinfo(res);
        set_error(error, error_len, "Could not connect to DoorMount device");
        return false;
    }

    freeaddrinfo(res);

    if (!send_all(sock, request, (size_t)request_len)) {
        close(sock);
        set_error(error, error_len, "Failed to send setup payload");
        return false;
    }

    char response[1024];
    size_t used = 0;
    memset(response, 0, sizeof(response));

    while (used + 1 < sizeof(response)) {
        ssize_t n = recv(sock, response + used, sizeof(response) - used - 1, 0);
        if (n <= 0) {
            break;
        }
        used += (size_t)n;
    }

    close(sock);

    if (!response_is_success(response)) {
        set_error(error, error_len, "DoorMount rejected setup payload");
        return false;
    }

    return true;
}
#endif

bool doormount_service_scan(DoormountNetworkList * out_list, char * error, size_t error_len) {
    if (out_list == NULL) {
        set_error(error, error_len, "Scan output buffer missing");
        return false;
    }

    memset(out_list, 0, sizeof(*out_list));
    set_error(error, error_len, "");

#ifdef _WIN32
    set_error(error, error_len, "Setup Doormount is supported on Linux only");
    return false;
#else
    FILE * pipe = popen(DOORMOUNT_SCAN_CMD, "r");
    if (pipe == NULL) {
        set_error(error, error_len, "Could not scan Wi-Fi networks");
        log_doormount("scan failed: popen returned NULL");
        return false;
    }

    char line[128];
    while (fgets(line, sizeof(line), pipe) != NULL) {
        trim_ascii(line);
        if (line[0] == '\0') {
            continue;
        }

        if (!starts_with(line, DOORMOUNT_AP_PREFIX)) {
            continue;
        }

        if (list_contains_ssid(out_list, line)) {
            continue;
        }

        if (out_list->count >= DOORMOUNT_MAX_NETWORKS) {
            break;
        }

        copy_text_safe(out_list->ssids[out_list->count], sizeof(out_list->ssids[out_list->count]), line);
        out_list->count++;
    }

    int rc = pclose(pipe);
    if (rc != 0) {
        log_doormount("scan warning: nmcli returned non-zero status");
    }

    if (out_list->count == 0) {
        set_error(error, error_len, "No DoorMount devices found");
        return false;
    }

    return true;
#endif
}

bool doormount_service_setup_selected(const char * doormount_ssid, char * error, size_t error_len) {
    if (doormount_ssid == NULL || doormount_ssid[0] == '\0') {
        set_error(error, error_len, "Select a DoorMount network first");
        return false;
    }

    if (!starts_with(doormount_ssid, DOORMOUNT_AP_PREFIX)) {
        set_error(error, error_len, "Selected SSID is not a DoorMount network");
        return false;
    }

#ifdef _WIN32
    (void)error;
    (void)error_len;
    set_error(error, error_len, "Setup Doormount is supported on Linux only");
    return false;
#else
    const DeviceConfig * config = device_config_get();
    if (config == NULL) {
        set_error(error, error_len, "Device config is unavailable");
        return false;
    }

    if (!device_config_has_wifi_credentials()) {
        set_error(error, error_len, "Home Wi-Fi credentials are missing");
        return false;
    }

    if (config->user_id <= 0) {
        set_error(error, error_len, "UserId is missing in saved config");
        return false;
    }

    if (contains_unsafe_shell_chars(doormount_ssid)) {
        set_error(error, error_len, "DoorMount SSID contains unsupported characters");
        return false;
    }

    log_doormount("doormount setup started");

    if (!connect_open_wifi(doormount_ssid)) {
        set_error(error, error_len, "Could not connect to selected DoorMount network");
        log_doormount("doormount setup failed: connect_open_wifi");
        return false;
    }

    sleep(2);

    char post_error[128] = {0};
    bool post_ok = post_doormount_setup(config, post_error, sizeof(post_error));

    bool reconnect_ok = connect_secured_wifi(config->wifi_ssid, config->wifi_password);
    if (!reconnect_ok) {
        set_error(error, error_len, "Failed to reconnect to home Wi-Fi after setup");
        log_doormount("doormount setup failed: reconnect home wifi");
        return false;
    }

    if (!post_ok) {
        set_error(error, error_len, post_error[0] != '\0' ? post_error : "DoorMount setup request failed");
        log_doormount("doormount setup failed: post_doormount_setup");
        return false;
    }

    log_doormount("doormount setup success");
    return true;
#endif
}
