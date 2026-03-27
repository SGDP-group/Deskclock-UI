#include "provisioning_service.h"

#include "device_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#define PROVISION_SERVER_PORT 8080
#define PROVISION_SERVER_BUF_SIZE 4096
#define SOFTAP_PROFILE_NAME "deskclock-softap"

static bool g_running = false;
static bool g_stop_requested = false;
static char g_softap_ssid[32] = "PiSetup-0000";

#ifndef _WIN32
static pthread_t g_server_thread;
#endif

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
        out[idx++] = *p;
        p++;
    }

    out[idx] = '\0';
    return idx > 0;
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

static int run_command(const char * cmd) {
    if (cmd == NULL || cmd[0] == '\0') {
        return -1;
    }

    return system(cmd);
}

static void make_softap_ssid(void) {
    unsigned seed = (unsigned)time(NULL);
    srand(seed);
    unsigned suffix = (unsigned)(rand() % 65536U);
    snprintf(g_softap_ssid, sizeof(g_softap_ssid), "PiSetup-%04X", suffix & 0xFFFFU);
}

#ifndef _WIN32
static bool start_softap(void) {
    char cmd[256];

    (void)run_command("nmcli connection delete " SOFTAP_PROFILE_NAME " > /dev/null 2>&1");

    snprintf(cmd,
             sizeof(cmd),
             "nmcli connection add type wifi ifname wlan0 con-name %s autoconnect no ssid %s > /dev/null 2>&1",
             SOFTAP_PROFILE_NAME,
             g_softap_ssid);
    if (run_command(cmd) != 0) {
        return false;
    }

    snprintf(cmd,
             sizeof(cmd),
             "nmcli connection modify %s 802-11-wireless.mode ap 802-11-wireless.band bg ipv4.method shared ipv4.addresses 192.168.4.1/24 wifi-sec.key-mgmt none > /dev/null 2>&1",
             SOFTAP_PROFILE_NAME);
    if (run_command(cmd) != 0) {
        return false;
    }

    snprintf(cmd, sizeof(cmd), "nmcli connection up %s > /dev/null 2>&1", SOFTAP_PROFILE_NAME);
    return run_command(cmd) == 0;
}

static void stop_softap(void) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "nmcli connection down %s > /dev/null 2>&1", SOFTAP_PROFILE_NAME);
    (void)run_command(cmd);
}

static bool connect_station_wifi(const char * ssid, const char * password) {
    if (ssid == NULL || password == NULL) {
        return false;
    }

    if (contains_unsafe_shell_chars(ssid) || contains_unsafe_shell_chars(password)) {
        return false;
    }

    char cmd[320];
    snprintf(cmd,
             sizeof(cmd),
             "nmcli dev wifi connect \"%s\" password \"%s\" ifname wlan0 > /dev/null 2>&1",
             ssid,
             password);

    return run_command(cmd) == 0;
}

static void send_http_response(int fd, int code, const char * status, const char * body) {
    char response[512];
    int body_len = (int)strlen(body);
    int len = snprintf(response,
                       sizeof(response),
                       "HTTP/1.1 %d %s\r\n"
                       "Content-Type: application/json\r\n"
                       "Connection: close\r\n"
                       "Content-Length: %d\r\n\r\n"
                       "%s",
                       code,
                       status,
                       body_len,
                       body);

    if (len > 0) {
        (void)send(fd, response, (size_t)len, 0);
    }
}

static void handle_post_provision(int fd, const char * request) {
    const char * body = strstr(request, "\r\n\r\n");
    if (body == NULL) {
        send_http_response(fd, 400, "Bad Request", "{\"status\":\"error\",\"message\":\"invalid request\"}");
        return;
    }

    body += 4;

    char wifi_ssid[DEVICE_CONFIG_WIFI_MAX_LEN] = {0};
    char wifi_password[DEVICE_CONFIG_WIFI_MAX_LEN] = {0};
    int user_id = 0;

    if (!json_get_string(body, "wifiSsid", wifi_ssid, sizeof(wifi_ssid)) ||
        !json_get_string(body, "wifiPassword", wifi_password, sizeof(wifi_password)) ||
        !json_get_int(body, "userId", &user_id)) {
        send_http_response(fd, 400, "Bad Request", "{\"status\":\"error\",\"message\":\"missing fields\"}");
        return;
    }

    if (strlen(wifi_ssid) == 0 || strlen(wifi_ssid) > 32 ||
        strlen(wifi_password) < 8 || strlen(wifi_password) > 63 || user_id <= 0) {
        send_http_response(fd, 400, "Bad Request", "{\"status\":\"error\",\"message\":\"invalid payload\"}");
        return;
    }

    DeviceConfig config = *device_config_get();
    config.user_id = user_id;
    config.provisioned = false;
    strncpy(config.wifi_ssid, wifi_ssid, sizeof(config.wifi_ssid) - 1);
    config.wifi_ssid[sizeof(config.wifi_ssid) - 1] = '\0';
    strncpy(config.wifi_password, wifi_password, sizeof(config.wifi_password) - 1);
    config.wifi_password[sizeof(config.wifi_password) - 1] = '\0';

    if (!device_config_save(&config)) {
        send_http_response(fd, 500, "Internal Server Error", "{\"status\":\"error\",\"message\":\"save failed\"}");
        return;
    }

    stop_softap();
    if (!connect_station_wifi(config.wifi_ssid, config.wifi_password)) {
        (void)start_softap();
        send_http_response(fd, 500, "Internal Server Error", "{\"status\":\"error\",\"message\":\"wifi connect failed\"}");
        return;
    }

    config.provisioned = true;
    (void)device_config_save(&config);

    send_http_response(fd, 200, "OK", "{\"status\":\"success\"}");
    g_stop_requested = true;
}

static void handle_client(int fd) {
    char request[PROVISION_SERVER_BUF_SIZE];
    ssize_t n = recv(fd, request, sizeof(request) - 1, 0);
    if (n <= 0) {
        return;
    }

    request[n] = '\0';

    if (strncmp(request, "GET /health", 11) == 0) {
        send_http_response(fd, 200, "OK", "{\"status\":\"ok\"}");
        return;
    }

    if (strncmp(request, "POST /api/provision", 19) == 0) {
        handle_post_provision(fd, request);
        return;
    }

    send_http_response(fd, 404, "Not Found", "{\"status\":\"error\",\"message\":\"not found\"}");
}

static void * server_thread_main(void * arg) {
    (void)arg;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        g_running = false;
        return NULL;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PROVISION_SERVER_PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(server_fd, 4) < 0) {
        close(server_fd);
        g_running = false;
        return NULL;
    }

    while (!g_stop_requested) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            continue;
        }

        handle_client(client_fd);
        close(client_fd);
    }

    close(server_fd);
    g_running = false;
    return NULL;
}
#endif

bool provisioning_service_start_if_needed(void) {
    if (device_config_is_provisioned()) {
        return false;
    }

#ifdef _WIN32
    return false;
#else
    if (g_running) {
        return true;
    }

    make_softap_ssid();
    if (!start_softap()) {
        return false;
    }

    g_stop_requested = false;
    g_running = true;

    if (pthread_create(&g_server_thread, NULL, server_thread_main, NULL) != 0) {
        g_running = false;
        stop_softap();
        return false;
    }

    pthread_detach(g_server_thread);
    return true;
#endif
}

void provisioning_service_stop(void) {
#ifdef _WIN32
    g_running = false;
#else
    if (!g_running) {
        return;
    }

    g_stop_requested = true;
    stop_softap();
#endif
}

bool provisioning_service_is_running(void) {
    return g_running;
}

const char * provisioning_service_get_softap_ssid(void) {
    return g_softap_ssid;
}
