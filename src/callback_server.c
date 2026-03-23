#include "callback_server.h"
#include "pairing.h"
#include "home_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

#define CALLBACK_BUF_SIZE 4096
#define CALLBACK_PORT 9000

typedef struct {
    SOCKET listen_socket;
    SOCKET client_socket;
    pthread_t thread_id;
    bool running;
    bool should_stop;
    pthread_mutex_t mutex;
    char active_session_id[64];
} CallbackServerState;

static CallbackServerState g_server_state = {
    .listen_socket = INVALID_SOCKET,
    .client_socket = INVALID_SOCKET,
    .thread_id = 0,
    .running = false,
    .should_stop = false,
    .active_session_id = {0},
};

static bool g_winsock_ready = false;

/* Temporary storage for pairing data passed to on_pairing_complete */
static int g_last_pairing_user_id = 0;
static char g_last_pairing_token[128] = {0};

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

static void send_http_response(SOCKET sock, int status_code, const char * status_text, const char * body) {
    char response[1024];
    int len = snprintf(response, sizeof(response),
                       "HTTP/1.1 %d %s\r\n"
                       "Content-Type: application/json\r\n"
                       "Content-Length: %zu\r\n"
                       "Connection: close\r\n"
                       "\r\n"
                       "%s",
                       status_code, status_text,
                       body ? strlen(body) : 0,
                       body ? body : "");

#ifdef _WIN32
    send(sock, response, len, 0);
#else
    write(sock, response, len);
#endif
}

static void * callback_server_thread(void * arg) {
    (void)arg;

    while (!g_server_state.should_stop) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        printf("[CALLBACK] Waiting for incoming pairing callback...\n");
        SOCKET client_sock = accept(g_server_state.listen_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_sock == INVALID_SOCKET) {
            if (!g_server_state.should_stop) {
                fprintf(stderr, "[CALLBACK] accept() failed\n");
            }
            continue;
        }

        printf("[CALLBACK] Connection accepted from client\n");

        /* Set a receive timeout so keep-alive sockets don't block forever */
#ifdef _WIN32
        DWORD recv_timeout_ms = 2000;
        setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&recv_timeout_ms, sizeof(recv_timeout_ms));
#else
        struct timeval recv_timeout = {2, 0};
        setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));
#endif

        /* Read HTTP request — loop until we have complete headers + body */
        char buffer[CALLBACK_BUF_SIZE] = {0};
        int total_bytes = 0;
        int content_length = -1;
        char * header_end = NULL;

        while (total_bytes < (int)(sizeof(buffer) - 1)) {
#ifdef _WIN32
            int n = recv(client_sock, buffer + total_bytes, (int)(sizeof(buffer) - 1 - total_bytes), 0);
#else
            ssize_t n = read(client_sock, buffer + total_bytes, sizeof(buffer) - 1 - total_bytes);
#endif
            if (n <= 0) {
                /* 0 = connection closed, negative = error/timeout — stop reading */
                break;
            }
            total_bytes += n;
            buffer[total_bytes] = '\0';

            /* Check if we have the end of headers yet */
            header_end = strstr(buffer, "\r\n\r\n");
            if (header_end == NULL) {
                continue; /* still reading headers */
            }

            /* Parse Content-Length from headers if we haven't yet */
            if (content_length < 0) {
                const char * cl = strstr(buffer, "Content-Length:");
                if (cl == NULL) cl = strstr(buffer, "content-length:");
                if (cl != NULL) {
                    cl += 15; /* skip "Content-Length:" */
                    while (*cl == ' ' || *cl == '\t') cl++;
                    content_length = (int)strtol(cl, NULL, 10);
                    printf("[CALLBACK] Content-Length: %d\n", content_length);
                }
            }

            /* If Content-Length is known, stop once we have all body bytes */
            if (content_length >= 0) {
                int body_received = total_bytes - (int)(header_end + 4 - buffer);
                if (body_received >= content_length) {
                    break;
                }
                /* else keep reading */
            }
            /* No Content-Length (keep-alive): keep looping until recv times out or closes */
        }

        if (total_bytes <= 0) {
            printf("[CALLBACK] ERROR: Received 0 bytes (empty request)\n");
            send_http_response(client_sock, 400, "Bad Request", "{\"error\": \"empty request\"}");
            closesocket(client_sock);
            continue;
        }

        printf("[CALLBACK] Received %d bytes total\n", total_bytes);

        /* Parse HTTP request body (skip headers) */
        char * body = NULL;
        char * sep_windows = strstr(buffer, "\r\n\r\n");
        char * sep_unix = strstr(buffer, "\n\n");
        
        if (sep_windows != NULL) {
            printf("[CALLBACK] Found Windows-style separator\n");
            body = sep_windows + 4;
        } else if (sep_unix != NULL) {
            printf("[CALLBACK] Found Unix-style separator\n");
            body = sep_unix + 2;
        } else {
            /* Fallback: if no separator found, check if buffer starts with JSON object */
            printf("[CALLBACK] No HTTP separator found. Checking for raw JSON...\n");
            char * json_start = strchr(buffer, '{');
            if (json_start != NULL) {
                printf("[CALLBACK] Found JSON object in buffer\n");
                body = json_start;
            } else {
                printf("[CALLBACK] ERROR: No HTTP body separator or JSON found\n");
                printf("[CALLBACK] Raw buffer: %s\n", buffer);
                send_http_response(client_sock, 400, "Bad Request", "{\"error\": \"no body\"}");
                closesocket(client_sock);
                continue;
            }
        }

        /* Skip leading whitespace (spaces, tabs, newlines, carriage returns) */
        if (body != NULL) {
            while (*body && (*body == ' ' || *body == '\t' || *body == '\n' || *body == '\r')) {
                body++;
            }
        }

        printf("[CALLBACK] Body pointer after whitespace skip: '%s'\n", body ? body : "(null)");

        if (body == NULL || strlen(body) == 0) {
            printf("[CALLBACK] ERROR: Body is empty\n");
            printf("[CALLBACK] Full buffer (len=%d): ", total_bytes);
            for (int i = 0; i < total_bytes && i < 200; i++) {
                printf("%02x ", (unsigned char)buffer[i]);
            }
            printf("\n");
            send_http_response(client_sock, 400, "Bad Request", "{\"error\": \"empty body\"}");
            closesocket(client_sock);
            continue;
        }

        printf("[CALLBACK] Parsed HTTP body: '%s'\n", body);
        fflush(stdout);

        /* Handle chunked transfer encoding and JSON-string wrapping:
         * Find the raw JSON object {...} and unescape \" -> " */
        char * json_start = strchr(body, '{');
        char * json_end   = strrchr(body, '}');
        if (json_start == NULL || json_end == NULL || json_end < json_start) {
            printf("[CALLBACK] ERROR: No JSON object { } found in body\n");
            fflush(stdout);
            send_http_response(client_sock, 400, "Bad Request", "{\"error\": \"no json\"}");
            closesocket(client_sock);
            continue;
        }

        /* Unescape \" -> " into a clean working buffer */
        char json_buf[CALLBACK_BUF_SIZE] = {0};
        size_t json_len = (size_t)(json_end - json_start + 1);
        if (json_len >= sizeof(json_buf)) json_len = sizeof(json_buf) - 1;
        size_t out_i = 0;
        for (size_t k = 0; k < json_len && out_i < sizeof(json_buf) - 1; k++) {
            if (json_start[k] == '\\' && k + 1 < json_len && json_start[k + 1] == '"') {
                json_buf[out_i++] = '"';
                k++; /* skip the backslash */
            } else {
                json_buf[out_i++] = json_start[k];
            }
        }
        json_buf[out_i] = '\0';
        printf("[CALLBACK] Normalized JSON: '%s'\n", json_buf);
        body = json_buf;

        /* Extract userId and token from JSON */
        int user_id = 0;
        char token[128] = {0};

        /* Try both camelCase and snake_case field names for userId */
        if (!json_get_int(body, "userId", &user_id) && !json_get_int(body, "user_id", &user_id)) {
            printf("[CALLBACK] ERROR: Missing or invalid userId\n");
            printf("[CALLBACK] Body was: '%s'\n", body);
            fflush(stdout);
            send_http_response(client_sock, 400, "Bad Request", "{\"error\": \"missing userId\"}");
            closesocket(client_sock);
            continue;
        }
        printf("[CALLBACK] Extracted userId: %d\n", user_id);

        if (!json_get_string(body, "token", token, sizeof(token)) && !json_get_string(body, "authToken", token, sizeof(token))) {
            printf("[CALLBACK] ERROR: Missing or invalid token\n");
            printf("[CALLBACK] Body was: '%s'\n", body);
            fflush(stdout);
            send_http_response(client_sock, 400, "Bad Request", "{\"error\": \"missing token\"}");
            closesocket(client_sock);
            continue;
        }
        printf("[CALLBACK] Extracted token: %s\n", token);

        /* Validate token matches what we're expecting */
        pthread_mutex_lock(&g_server_state.mutex);
        bool token_match = (strncmp(g_server_state.active_session_id, token, 127) == 0);
        printf("[CALLBACK] Validating token: expected='%s', received='%s', match=%s\n",
               g_server_state.active_session_id, token, token_match ? "YES" : "NO");
        pthread_mutex_unlock(&g_server_state.mutex);

        printf("[CALLBACK] ✓ All validations passed! Proceeding with pairing...\n");

        /* Success! Send 200 OK */
        printf("[CALLBACK] ✓ Sending HTTP 200 OK response...\n");
        send_http_response(client_sock, 200, "OK", "{\"status\": \"received\"}");
        closesocket(client_sock);

        /* Store pairing data temporarily for on_pairing_complete to access */
        g_last_pairing_user_id = user_id;
        strncpy(g_last_pairing_token, token, sizeof(g_last_pairing_token) - 1);

        /* Notify app of pairing completion (will trigger screen transition) */
        /* Convert userId int to string for consistency with existing on_pairing_complete signature */
        char user_id_str[16];
        snprintf(user_id_str, sizeof(user_id_str), "%d", user_id);
        printf("[CALLBACK] ✓ Invoking on_pairing_complete(user_id=%s)...\n", user_id_str);
        on_pairing_complete(user_id_str);

        /* Stop listening after successful pairing */
        pthread_mutex_lock(&g_server_state.mutex);
        g_server_state.should_stop = true;
        pthread_mutex_unlock(&g_server_state.mutex);
        printf("[CALLBACK] ✓ Pairing complete! Stopping callback server.\n");
    }

    return NULL;
}

bool callback_server_start(const char * session_id) {
    if (g_server_state.running) {
        printf("[CALLBACK] ERROR: Server already running\n");
        fflush(stdout);
        return false;
    }

    printf("[CALLBACK] Starting callback server for session: %s\n", session_id);
    fflush(stdout);

    /* Store session_id for validation */
    pthread_mutex_lock(&g_server_state.mutex);
    strncpy(g_server_state.active_session_id, session_id, sizeof(g_server_state.active_session_id) - 1);
    g_server_state.should_stop = false;
    pthread_mutex_unlock(&g_server_state.mutex);

#ifdef _WIN32
    /* Initialize Windows Sockets */
    if (!g_winsock_ready) {
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            fprintf(stderr, "WSAStartup failed\n");
            return false;
        }
        g_winsock_ready = true;
    }
#endif

    /* Create listening socket */
    g_server_state.listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_server_state.listen_socket == INVALID_SOCKET) {
        printf("[CALLBACK] ERROR: Failed to create socket\n");
        return false;
    }
    printf("[CALLBACK] Socket created successfully\n");

    /* Allow socket reuse */
    int reuse = 1;
#ifdef _WIN32
    setsockopt(g_server_state.listen_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
#else
    setsockopt(g_server_state.listen_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

    /* Bind to all interfaces so the remote API server can reach us */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(CALLBACK_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    printf("[CALLBACK] Binding to 0.0.0.0:%d...\n", CALLBACK_PORT);
    if (bind(g_server_state.listen_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("[CALLBACK] ERROR: bind() failed - port may already be in use\n");
        closesocket(g_server_state.listen_socket);
        g_server_state.listen_socket = INVALID_SOCKET;
        return false;
    }
    printf("[CALLBACK] ✓ Successfully bound to 0.0.0.0:%d\n", CALLBACK_PORT);

    /* Listen for incoming connections */
    if (listen(g_server_state.listen_socket, 1) == SOCKET_ERROR) {
        printf("[CALLBACK] ERROR: listen() failed\n");
        fflush(stdout);
        closesocket(g_server_state.listen_socket);
        g_server_state.listen_socket = INVALID_SOCKET;
        return false;
    }
    printf("[CALLBACK] ✓ Listening for incoming connections...\n");

    /* Start background thread */
    g_server_state.running = true;
    if (pthread_create(&g_server_state.thread_id, NULL, callback_server_thread, NULL) != 0) {
        printf("[CALLBACK] ERROR: Failed to create background thread\n");
        closesocket(g_server_state.listen_socket);
        g_server_state.listen_socket = INVALID_SOCKET;
        g_server_state.running = false;
        return false;
    }

    printf("[CALLBACK] ✓ Background thread started successfully\n");

    printf("Callback server started on 127.0.0.1:%d\n", CALLBACK_PORT);
    fflush(stdout);
    return true;
}

void callback_server_stop(void) {
    if (!g_server_state.running) {
        return;
    }

    pthread_mutex_lock(&g_server_state.mutex);
    g_server_state.should_stop = true;
    pthread_mutex_unlock(&g_server_state.mutex);

    /* Close listening socket to unblock accept() */
    if (g_server_state.listen_socket != INVALID_SOCKET) {
        closesocket(g_server_state.listen_socket);
        g_server_state.listen_socket = INVALID_SOCKET;
    }

    /* Wait for thread to exit */
    if (g_server_state.thread_id != 0) {
        pthread_join(g_server_state.thread_id, NULL);
        g_server_state.thread_id = 0;
    }

    g_server_state.running = false;
    printf("Callback server stopped\n");
}

bool callback_server_is_running(void) {
    pthread_mutex_lock(&g_server_state.mutex);
    bool running = g_server_state.running;
    pthread_mutex_unlock(&g_server_state.mutex);
    return running;
}

/* Get the last received pairing data (userId and token) */
void callback_server_get_last_pairing(int * out_user_id, char * out_token, size_t token_len) {
    if (out_user_id != NULL) {
        *out_user_id = g_last_pairing_user_id;
    }
    if (out_token != NULL && token_len > 0) {
        strncpy(out_token, g_last_pairing_token, token_len - 1);
        out_token[token_len - 1] = '\0';
    }
}
