#include "pairing.h"
#include "callback_server.h"
#include "lvgl/lvgl.h"
#include <curl/curl.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "lvgl/src/libs/qrcode/qrcodegen.h"
#include "lvgl/src/stdlib/lv_mem.h"
#include "lvgl/src/misc/lv_async.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#define PAIRING_BASE_URL "http://127.0.0.1:8080"

typedef struct {
    char * data;
    size_t len;
} CurlBuffer;

static size_t curl_write_cb(char * ptr, size_t size, size_t nmemb, void * userdata) {
    size_t add = size * nmemb;
    CurlBuffer * buf = (CurlBuffer *)userdata;
    char * new_data = realloc(buf->data, buf->len + add + 1);
    if (!new_data) return 0;
    buf->data = new_data;
    memcpy(buf->data + buf->len, ptr, add);
    buf->len += add;
    buf->data[buf->len] = '\0';
    return add;
}

static bool curl_perform(CURL * curl, CurlBuffer * out_buf) {
    out_buf->data = NULL;
    out_buf->len = 0;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out_buf);
    CURLcode rc = curl_easy_perform(curl);
    return rc == CURLE_OK;
}

bool pairing_session_create(char * out_session_id, size_t out_len) {
    if (out_session_id == NULL || out_len == 0) return false;
    out_session_id[0] = '\0';

    CURL * curl = curl_easy_init();
    if (!curl) return false;

    char url[128];
    snprintf(url, sizeof(url), "%s/pairing/session", PAIRING_BASE_URL);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");

    CurlBuffer buf;
    bool ok = curl_perform(curl, &buf);
    curl_easy_cleanup(curl);
    if (!ok || buf.data == NULL) {
        free(buf.data);
        return false;
    }

    /* Manual JSON parsing: look for "sessionId":"<value>" */
    bool got = false;
    const char * p = strstr(buf.data, "\"sessionId\"");
    if (p != NULL) {
        p = strchr(p, ':');
        if (p != NULL) {
            p++;
            while (*p && (*p == ' ' || *p == '\t')) {
                p++;
            }
            if (*p == '"') {
                p++;
                size_t i = 0;
                while (i < out_len - 1 && *p && *p != '"') {
                    out_session_id[i++] = *p++;
                }
                out_session_id[i] = '\0';
                got = (i > 0);
            }
        }
    }

    free(buf.data);
    return got;
}

bool qr_render_lvgl(const char * qr_data) {
    if (!qr_data) return false;

    uint8_t qrcode[qrcodegen_BUFFER_LEN_MAX];
    uint8_t temp[qrcodegen_BUFFER_LEN_MAX];

    bool ok = qrcodegen_encodeText(qr_data, temp, qrcode, qrcodegen_Ecc_LOW,
                                   qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true);
    if (!ok) return false;

    int size = qrcodegen_getSize(qrcode);
    if (size <= 0) return false;

    lv_obj_t * screen = lv_screen_active();
    lv_obj_t * canvas = lv_canvas_create(screen);
    int scale = 5;
    int img_size = size * scale;
    lv_color_t * buf = lv_malloc(img_size * img_size * sizeof(lv_color_t));
    if (!buf) {
        lv_obj_del(canvas);
        return false;
    }

    lv_canvas_set_buffer(canvas, buf, img_size, img_size, LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (qrcodegen_getModule(qrcode, x, y)) {
                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) {
                        lv_canvas_set_px(canvas, x * scale + dx, y * scale + dy, lv_color_black(), LV_OPA_COVER);
                    }
                }
            }
        }
    }

    lv_obj_center(canvas);
    return true;
}

typedef struct {
    char session_id[64];
} PairingPollCtx;

void pairing_screen_start(void) {
    char session_id[64] = {0};
    if (!pairing_session_create(session_id, sizeof(session_id))) {
        LV_LOG_ERROR("Could not create pairing session");
        return;
    }

    char qr_json[160];
    snprintf(qr_json, sizeof(qr_json), "{\"sid\":\"%s\",\"url\":\"%s\"}", session_id, PAIRING_BASE_URL);
    if (!qr_render_lvgl(qr_json)) {
        LV_LOG_ERROR("Failed to render QR");
        return;
    }

    /* Start callback server instead of polling */
    if (!callback_server_start(session_id)) {
        LV_LOG_ERROR("Failed to start callback server");
        pairing_screen_start();  /* Fall back to polling by restarting */
        return;
    }

    LV_LOG_INFO("Pairing session started: %s", session_id);
}

/* Default weak handler; application can override elsewhere. */
__attribute__((weak)) void on_pairing_complete(const char * user_id) {
    (void)user_id;
    LV_LOG_INFO("Pairing complete (default handler)");
}
