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
#else
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include "lvgl/lvgl.h"

#define QUEUE_DEPTH 32
#define MAX_CHUNK   512
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

