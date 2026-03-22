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

        /* Read HTTP request */
        char buffer[CALLBACK_BUF_SIZE] = {0};
#ifdef _WIN32
        int recv_bytes = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
#else
        ssize_t recv_bytes = read(client_sock, buffer, sizeof(buffer) - 1);
#endif

        if (recv_bytes <= 0) {
            printf("[CALLBACK] ERROR: Received %d bytes (empty request)\n", recv_bytes);
            send_http_response(client_sock, 400, "Bad Request", "{\"error\": \"empty request\"}");
            closesocket(client_sock);
            continue;
        }

        buffer[recv_bytes] = '\0';
        printf("[CALLBACK] Received %d bytes:\n%s\n", recv_bytes, buffer);

        /* Parse HTTP request body (skip headers) */
        char * body = strstr(buffer, "\r\n\r\n");
        if (body == NULL) {
            body = strstr(buffer, "\n\n");
            if (body != NULL) {
                body += 2;
            }
        } else {
            body += 4;
        }

        if (body == NULL || strlen(body) == 0) {
            printf("[CALLBACK] ERROR: No HTTP body found\n");
            send_http_response(client_sock, 400, "Bad Request", "{\"error\": \"no body\"}");
            closesocket(client_sock);
            continue;
        }

        printf("[CALLBACK] Parsed HTTP body: %s\n", body);

        /* Extract userId, token, sessionId from JSON */
        int user_id = 0;
        char token[128] = {0};
        char session_id[64] = {0};

        if (!json_get_int(body, "userId", &user_id)) {
            printf("[CALLBACK] ERROR: Missing or invalid userId\n");
            send_http_response(client_sock, 400, "Bad Request", "{\"error\": \"missing userId\"}");
            closesocket(client_sock);
            continue;
        }
        printf("[CALLBACK] Extracted userId: %d\n", user_id);

        if (!json_get_string(body, "token", token, sizeof(token))) {
            printf("[CALLBACK] ERROR: Missing or invalid token\n");
            send_http_response(client_sock, 400, "Bad Request", "{\"error\": \"missing token\"}");
            closesocket(client_sock);
            continue;
        }
        printf("[CALLBACK] Extracted token: %s\n", token);

        if (!json_get_string(body, "sessionId", session_id, sizeof(session_id))) {
            printf("[CALLBACK] ERROR: Missing or invalid sessionId\n");
            send_http_response(client_sock, 400, "Bad Request", "{\"error\": \"missing sessionId\"}");
            closesocket(client_sock);
            continue;
        }
        printf("[CALLBACK] Extracted sessionId: %s\n", session_id);

        /* Validate sessionId matches active pairing session */
        pthread_mutex_lock(&g_server_state.mutex);
        bool session_match = (strncmp(g_server_state.active_session_id, session_id, 63) == 0);
        printf("[CALLBACK] Validating sessionId: expected='%s', received='%s', match=%s\n",
               g_server_state.active_session_id, session_id, session_match ? "YES" : "NO");
        pthread_mutex_unlock(&g_server_state.mutex);

        if (!session_match) {
            printf("[CALLBACK] ERROR: Session ID mismatch! Rejecting callback.\n");
            send_http_response(client_sock, 403, "Forbidden", "{\"error\": \"session id mismatch\"}");
            closesocket(client_sock);
            continue;
        }

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
        return false;
    }

    printf("[CALLBACK] Starting callback server for session: %s\n", session_id);

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

    /* Bind to localhost:9000 */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(CALLBACK_PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    printf("[CALLBACK] Binding to 127.0.0.1:%d...\n", CALLBACK_PORT);
    if (bind(g_server_state.listen_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("[CALLBACK] ERROR: bind() failed - port may already be in use\n");
        closesocket(g_server_state.listen_socket);
        g_server_state.listen_socket = INVALID_SOCKET;
        return false;
    }
    printf("[CALLBACK] ✓ Successfully bound to 127.0.0.1:%d\n", CALLBACK_PORT);

    /* Listen for incoming connections */
    if (listen(g_server_state.listen_socket, 1) == SOCKET_ERROR) {
        printf("[CALLBACK] ERROR: listen() failed\n");
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
