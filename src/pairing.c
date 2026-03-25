#include "pairing.h"
#include "callback_server.h"
#include "home_config.h"
#include "src/home_api_client.h"
#include "lvgl/lvgl.h"
#include <curl/curl.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "lvgl/src/libs/qrcode/qrcodegen.h"
#include "lvgl/src/stdlib/lv_mem.h"
#include "lvgl/src/misc/lv_async.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

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
    snprintf(url, sizeof(url), "http://%s:%d/pairing/session", HOME_API_HOST, HOME_API_PORT);
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
    printf("[QR] Rendering QR code...\n");
    printf("[QR]   Input data: %s\n", qr_data ? qr_data : "NULL");
    
    if (!qr_data) {
        printf("[QR] ERROR: qr_data is NULL\n");
        return false;
    }

    printf("[QR] Step 1: Encoding text to QR...\n");
    uint8_t qrcode[qrcodegen_BUFFER_LEN_MAX];
    uint8_t temp[qrcodegen_BUFFER_LEN_MAX];

    bool ok = qrcodegen_encodeText(qr_data, temp, qrcode, qrcodegen_Ecc_LOW,
                                   qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true);
    if (!ok) {
        printf("[QR] ERROR: QR encoding failed (data too large?)\n");
        return false;
    }
    printf("[QR] ✓ QR encoded successfully\n");

    printf("[QR] Step 2: Getting QR size...\n");
    int size = qrcodegen_getSize(qrcode);
    printf("[QR]   QR size: %d modules\n", size);
    if (size <= 0) {
        printf("[QR] ERROR: Invalid QR size: %d\n", size);
        return false;
    }

    printf("[QR] Step 3: Creating LVGL canvas...\n");
    lv_obj_t * screen = lv_screen_active();
    if (!screen) {
        printf("[QR] ERROR: No active screen\n");
        return false;
    }
    
    lv_obj_t * canvas = lv_canvas_create(screen);
    if (!canvas) {
        printf("[QR] ERROR: Failed to create canvas\n");
        return false;
    }

    printf("[QR] Step 4: Allocating buffer...\n");
    int scale = 2;
    int img_size = size * scale;
    printf("[QR]   Canvas: %d x %d pixels (%zu bytes)\n", img_size, img_size, (size_t)img_size * img_size * sizeof(lv_color_t));
    
    lv_color_t * buf = lv_malloc(img_size * img_size * sizeof(lv_color_t));
    if (!buf) {
        printf("[QR] ERROR: Memory allocation failed\n");
        lv_obj_del(canvas);
        return false;
    }

    printf("[QR] Step 5: Setting up canvas and background...\n");
    lv_canvas_set_buffer(canvas, buf, img_size, img_size, LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);

    printf("[QR] Step 6: Drawing QR modules...\n");
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

    printf("[QR] Step 7: Centering canvas...\n");
    lv_obj_center(canvas);
    printf("[QR] ✓ QR code rendering complete\n");
    
    return true;
}

typedef struct {
    char session_id[64];
} PairingPollCtx;

static void generate_session_id(char * out_id, size_t out_len) {
    if (out_id == NULL || out_len < 16) {
        fprintf(stderr, "[PAIRING] ERROR: Invalid parameter to generate_session_id\n");
        return;
    }
    
    /* Generate session ID from timestamp + random number */
    time_t now = time(NULL);
    srand((unsigned int)now);
    int rand_num = rand();
    
    int written = snprintf(out_id, out_len, "session-%ld-%u", (long)now, (unsigned int)abs(rand_num % 100000));
    if (written < 0 || written >= (int)out_len) {
        fprintf(stderr, "[PAIRING] ERROR: Failed to generate session ID\n");
        out_id[0] = '\0';
        return;
    }
    
    printf("[PAIRING] ✓ Generated session ID locally: %s\n", out_id);
}

void pairing_screen_start(void) {
    printf("\n");
    printf("========================================\n");
    printf("[PAIRING] *** STARTING PAIRING FLOW ***\n");
    printf("========================================\n");
    
    printf("[PAIRING] Step 1: Fetching auth token from backend...\n");
    char token[128] = {0};
    bool got_token = home_api_fetch_auth_token(token, sizeof(token));
    
    if (!got_token || token[0] == '\0') {
        printf("[PAIRING] FATAL ERROR: Failed to fetch auth token\n");
        printf("[PAIRING]   Backend: http://%s:%d\n", HOME_API_HOST, HOME_API_PORT);
        LV_LOG_ERROR("Failed to fetch auth token");
        return;
    }
    printf("[PAIRING] ✓ Received token: %s\n", token);

    printf("[PAIRING] Step 2: Generating QR code with token...\n");
    if (!qr_render_lvgl(token)) {
        printf("[PAIRING] FATAL ERROR: Failed to render QR code\n");
        LV_LOG_ERROR("Failed to render QR");
        return;
    }
    printf("[PAIRING] ✓ QR code rendered successfully\n");

    printf("[PAIRING] Step 3: Starting callback server on port %d...\n", HOME_CALLBACK_SERVER_PORT);
    if (!callback_server_start(token)) {
        printf("[PAIRING] FATAL ERROR: Failed to start callback server\n");
        fprintf(stderr, "[PAIRING] Port %d may already be in use\n", HOME_CALLBACK_SERVER_PORT);
        LV_LOG_ERROR("Failed to start callback server");
        return;
    }
    printf("[PAIRING] ✓ Callback server started successfully\n");

    printf("\n");
    printf("========================================\n");
    printf("[PAIRING] *** PAIRING SESSION READY ***\n");
    printf("========================================\n");
    printf("[PAIRING] Token:             %s\n", token);
    printf("[PAIRING] Device IP:         %s\n", HOME_DEVICE_IP);
    printf("[PAIRING] Callback Port:     %d\n", HOME_CALLBACK_SERVER_PORT);
    printf("[PAIRING] Callback URL:      http://%s:%d/pairing/callback\n", HOME_DEVICE_IP, HOME_CALLBACK_SERVER_PORT);
    printf("[PAIRING]\n");
    printf("[PAIRING] Backend should POST to callback URL with:\n");
    printf("[PAIRING]   { \"userId\": <id>, \"token\": \"%s\" }\n", token);
    printf("[PAIRING]\n");
    printf("[PAIRING] Waiting for backend callback...\n");
    printf("========================================\n\n");
    
    LV_LOG_INFO("Pairing session ready for token: %s", token);
}

/* Default weak handler; application can override elsewhere. */
__attribute__((weak)) void on_pairing_complete(const char * user_id) {
    (void)user_id;
    LV_LOG_INFO("Pairing complete (default handler)");
}
