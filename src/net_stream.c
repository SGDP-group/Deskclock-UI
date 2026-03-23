#include "net_stream.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#ifdef _WIN32
/* Stub on Windows builds used for desktop preview; Pi build uses POSIX path. */
bool net_stream_start(const char * host, uint16_t port) { (void)host; (void)port; return false; }
void net_stream_stop(void) {}
bool net_stream_enqueue(const void * data, size_t len) { (void)data; (void)len; return false; }
bool net_stream_is_connected(void) { return false; }
size_t net_stream_queue_depth(void) { return 0; }
#else
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include "lvgl/lvgl.h"
#include "home_config.h"

#define QUEUE_DEPTH 8
#define MAX_CHUNK   HOME_GAZE_STREAM_MAX_PACKET_BYTES
#define RECONNECT_BACKOFF_MS 1000

typedef struct {
    size_t len;
    uint8_t data[MAX_CHUNK];
} Chunk;

static Chunk queue_buf[QUEUE_DEPTH];
static size_t q_head = 0;
static size_t q_tail = 0;
static size_t q_count = 0;

static pthread_mutex_t q_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  q_cv    = PTHREAD_COND_INITIALIZER;

static pthread_t worker;
static int sock_fd = -1;
static bool running = false;
static char target_host[64];
static uint16_t target_port = 0;

static void close_socket(void) {
    if (sock_fd >= 0) {
        close(sock_fd);
        sock_fd = -1;
    }
}

static int dial_server(void) {
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", target_port);

    struct addrinfo hints = {0};
    hints.ai_family   = AF_INET;      /* Pi Zero W: IPv4 is plenty */
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo * res = NULL;
    int ret = getaddrinfo(target_host, port_str, &hints, &res);
    if (ret != 0 || res == NULL) {
        return -1;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return -1;
    }

    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        close(fd);
        freeaddrinfo(res);
        return -1;
    }

    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    freeaddrinfo(res);
    return fd;
}

static bool send_all(int fd, const uint8_t * data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EINTR)) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            usleep(5 * 1000);
            continue;
        }
        return false; /* fatal */
    }
    return true;
}

static void * worker_thread(void * arg) {
    (void)arg;

    while (running) {
        pthread_mutex_lock(&q_mutex);
        while (q_count == 0 && running) {
            pthread_cond_wait(&q_cv, &q_mutex);
        }
        if (!running) {
            pthread_mutex_unlock(&q_mutex);
            break;
        }

        Chunk chunk = queue_buf[q_tail];
        q_tail = (q_tail + 1) % QUEUE_DEPTH;
        q_count--;
        pthread_mutex_unlock(&q_mutex);

        if (sock_fd < 0) {
            sock_fd = dial_server();
            if (sock_fd < 0) {
                LV_LOG_WARN("net_stream: connect failed, retrying");
                usleep(RECONNECT_BACKOFF_MS * 1000);
                /* push chunk back to front of queue */
                pthread_mutex_lock(&q_mutex);
                if (q_count < QUEUE_DEPTH) {
                    q_head = (q_head == 0) ? (QUEUE_DEPTH - 1) : (q_head - 1);
                    queue_buf[q_head] = chunk;
                    q_count++;
                }
                pthread_mutex_unlock(&q_mutex);
                continue;
            }
        }

        if (!send_all(sock_fd, chunk.data, chunk.len)) {
            LV_LOG_WARN("net_stream: send failed, reconnecting");
            close_socket();
            /* requeue the unsent chunk */
            pthread_mutex_lock(&q_mutex);
            if (q_count < QUEUE_DEPTH) {
                q_head = (q_head == 0) ? (QUEUE_DEPTH - 1) : (q_head - 1);
                queue_buf[q_head] = chunk;
                q_count++;
            }
            pthread_mutex_unlock(&q_mutex);
            usleep(RECONNECT_BACKOFF_MS * 1000);
        }
    }

    close_socket();
    return NULL;
}

bool net_stream_start(const char * host, uint16_t port) {
    if (!host || port == 0) return false;

    pthread_mutex_lock(&q_mutex);
    if (running) {
        pthread_mutex_unlock(&q_mutex);
        return true; /* already running */
    }

    strncpy(target_host, host, sizeof(target_host) - 1);
    target_host[sizeof(target_host) - 1] = '\0';
    target_port = port;

    q_head = q_tail = q_count = 0;
    running = true;
    int rc = pthread_create(&worker, NULL, worker_thread, NULL);
    pthread_mutex_unlock(&q_mutex);

    if (rc != 0) {
        running = false;
        return false;
    }
    return true;
}

void net_stream_stop(void) {
    pthread_mutex_lock(&q_mutex);
    if (!running) {
        pthread_mutex_unlock(&q_mutex);
        return;
    }
    running = false;
    pthread_cond_signal(&q_cv);
    pthread_mutex_unlock(&q_mutex);

    pthread_join(worker, NULL);
    close_socket();
}

bool net_stream_enqueue(const void * data, size_t len) {
    if (!running || data == NULL || len == 0) return false;

    if (len > MAX_CHUNK) return false;

    pthread_mutex_lock(&q_mutex);
    if (q_count >= QUEUE_DEPTH) {
        pthread_mutex_unlock(&q_mutex);
        return false; /* queue full */
    }

    Chunk * c = &queue_buf[q_head];
    memcpy(c->data, data, len);
    c->len = len;

    q_head = (q_head + 1) % QUEUE_DEPTH;
    q_count++;
    pthread_cond_signal(&q_cv);
    pthread_mutex_unlock(&q_mutex);
    return true;
}

bool net_stream_is_connected(void) {
    bool connected = false;

    pthread_mutex_lock(&q_mutex);
    connected = (sock_fd >= 0);
    pthread_mutex_unlock(&q_mutex);

    return connected;
}

size_t net_stream_queue_depth(void) {
    size_t depth = 0;

    pthread_mutex_lock(&q_mutex);
    depth = q_count;
    pthread_mutex_unlock(&q_mutex);

    return depth;
}

#endif /* _WIN32 */
