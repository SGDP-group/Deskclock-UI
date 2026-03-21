#include "screen_home.h"
#include "lvgl/lvgl.h"
#include "../data/app_state.h"
#include "src/home_api_client.h"
#include "src/home_config.h"

#include <ctype.h>
#include <string.h>
#include <time.h>

extern const lv_font_t Antonio_bold_80;

typedef struct {
    lv_obj_t * card;
    lv_obj_t * title;
    lv_obj_t * subtitle;
    lv_obj_t * time_range;
    lv_obj_t * status;
} TaskCardRefs;

#define HOME_CARD_POOL_SIZE 6

static lv_obj_t * g_lbl_time = NULL;
static lv_obj_t * g_lbl_date = NULL;
static lv_obj_t * g_lbl_footer = NULL;
static lv_obj_t * g_task_list = NULL;
static TaskCardRefs g_cards[HOME_CARD_POOL_SIZE];
static int32_t g_card_height = 214;

static bool g_fetch_inflight = false;

static char g_last_time[16] = {0};
static char g_last_date[24] = {0};

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

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

static void uppercase_ascii(char * text) {
    if (text == NULL) {
        return;
    }
    for (size_t i = 0; text[i] != '\0'; i++) {
        text[i] = (char)toupper((unsigned char)text[i]);
    }
}

static void update_clock_labels(void) {
    if (g_lbl_time == NULL || g_lbl_date == NULL) {
        return;
    }

    time_t now = time(NULL);
    struct tm local_tm;
#ifdef _WIN32
    localtime_s(&local_tm, &now);
#else
    localtime_r(&now, &local_tm);
#endif

    char time_buf[16] = {0};
    char date_buf[24] = {0};

    strftime(time_buf, sizeof(time_buf), "%H:%M", &local_tm);
    strftime(date_buf, sizeof(date_buf), "%a, %d %b", &local_tm);
    uppercase_ascii(date_buf);

    if (strcmp(g_last_time, time_buf) != 0) {
        lv_label_set_text(g_lbl_time, time_buf);
        copy_text_safe(g_last_time, sizeof(g_last_time), time_buf);
    }

    if (strcmp(g_last_date, date_buf) != 0) {
        lv_label_set_text(g_lbl_date, date_buf);
        copy_text_safe(g_last_date, sizeof(g_last_date), date_buf);
    }
}

static void apply_carousel_depth(void) {
    if (g_task_list == NULL) {
        return;
    }

    /* Keep cards at stable styles while debugging AV during scroll. */
    for (uint8_t i = 0; i < HOME_CARD_POOL_SIZE; i++) {
        lv_obj_t * card = g_cards[i].card;
        if (card == NULL || lv_obj_has_flag(card, LV_OBJ_FLAG_HIDDEN)) {
            continue;
        }

        lv_obj_set_style_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_translate_x(card, 0, LV_PART_MAIN);
    }
}

static void render_loading_card(const char * title, const char * subtitle) {
    for (uint8_t i = 0; i < HOME_CARD_POOL_SIZE; i++) {
        if (g_cards[i].card == NULL) {
            continue;
        }

        if (i == 0) {
            lv_label_set_text(g_cards[i].title, title);
            lv_label_set_text(g_cards[i].subtitle, subtitle);
            lv_label_set_text(g_cards[i].time_range, "");
            lv_label_set_text(g_cards[i].status, "");
            lv_obj_clear_flag(g_cards[i].card, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_cards[i].card, LV_OBJ_FLAG_HIDDEN);
        }
    }

    apply_carousel_depth();
}

static void render_task_cards(void) {
    if (g_app_state.tasks_loading) {
        render_loading_card("Loading tasks", "Checking due-today");
        return;
    }

    if (g_app_state.home_task_count == 0) {
        render_loading_card("No tasks for now", "Enjoy the clear schedule");
        return;
    }

    uint8_t visible_count = g_app_state.home_task_count;
    if (visible_count > HOME_CARD_POOL_SIZE) {
        visible_count = HOME_CARD_POOL_SIZE;
    }

    for (uint8_t i = 0; i < HOME_CARD_POOL_SIZE; i++) {
        lv_obj_t * card = g_cards[i].card;
        if (card == NULL) {
            continue;
        }

        if (i >= visible_count) {
            lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        const HomeTask * task = &g_app_state.home_tasks[i];
        lv_label_set_text(g_cards[i].title, task->title);
        lv_label_set_text(g_cards[i].subtitle, task->subtitle);
        lv_label_set_text(g_cards[i].time_range, task->time_range);
        lv_label_set_text(g_cards[i].status, task->status);

        lv_obj_set_style_text_color(g_cards[i].status,
                                    task->completed ? lv_color_hex(0x8EF2A5) : lv_color_hex(0xD8DEE9),
                                    LV_PART_MAIN);

        lv_obj_clear_flag(card, LV_OBJ_FLAG_HIDDEN);
    }

    apply_carousel_depth();
}

static void clock_timer_cb(lv_timer_t * timer) {
    (void)timer;
    update_clock_labels();
}

static void fetch_due_today_now(void) {
    HomeApiTask api_tasks[APP_MAX_HOME_TASKS];
    HomeTask ui_tasks[APP_MAX_HOME_TASKS];
    memset(api_tasks, 0, sizeof(api_tasks));
    memset(ui_tasks, 0, sizeof(ui_tasks));

    uint8_t count = 0;
    bool ok = home_api_fetch_due_today(api_tasks, &count, HOME_CARD_POOL_SIZE);
    if (ok) {
        for (uint8_t i = 0; i < count; i++) {
            ui_tasks[i].id = api_tasks[i].id;
            ui_tasks[i].completed = api_tasks[i].completed;
            copy_text_safe(ui_tasks[i].title, sizeof(ui_tasks[i].title), api_tasks[i].title);
            copy_text_safe(ui_tasks[i].subtitle, sizeof(ui_tasks[i].subtitle), api_tasks[i].subtitle);
            copy_text_safe(ui_tasks[i].time_range, sizeof(ui_tasks[i].time_range), api_tasks[i].time_range);
            copy_text_safe(ui_tasks[i].status, sizeof(ui_tasks[i].status), api_tasks[i].status);
        }
    }

    app_state_set_tasks_loading(false);
    if (ok) {
        app_state_set_home_tasks(ui_tasks, count);
        app_state_set_status("Due-today refreshed");
    } else {
        app_state_set_home_tasks(NULL, 0);
        app_state_set_status("Could not reach API");
    }

    render_task_cards();
    if (g_lbl_footer != NULL) {
        lv_label_set_text(g_lbl_footer, g_app_state.status_message);
    }
}

static void start_due_today_fetch(void) {
    if (g_fetch_inflight) {
        return;
    }

    g_fetch_inflight = true;
    app_state_set_tasks_loading(true);
    render_task_cards();

    fetch_due_today_now();
    g_fetch_inflight = false;
}

static void refresh_timer_cb(lv_timer_t * timer) {
    (void)timer;
    start_due_today_fetch();
}

static void quick_focus_event(lv_event_t * e) {
    (void)e;
    app_state_set_status("Quick Focus ready");
    if (g_lbl_footer != NULL) {
        lv_label_set_text(g_lbl_footer, g_app_state.status_message);
    }
}

static void list_scroll_event(lv_event_t * e) {
    (void)e;
}

static void create_task_cards(lv_obj_t * parent) {
    for (uint8_t i = 0; i < HOME_CARD_POOL_SIZE; i++) {
        lv_obj_t * card = lv_obj_create(parent);
        lv_obj_set_width(card, lv_pct(100));
        lv_obj_set_height(card, g_card_height);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A1A), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(card, 30, LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(card, 0, LV_PART_MAIN);

        lv_obj_t * left_strip = lv_obj_create(card);
        lv_obj_set_size(left_strip, 16, lv_pct(100));
        lv_obj_align(left_strip, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_color(left_strip, lv_color_hex(0x10B981), LV_PART_MAIN);
        lv_obj_set_style_border_width(left_strip, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(left_strip, LV_RADIUS_CIRCLE, LV_PART_MAIN);

        lv_obj_t * start_slab = lv_obj_create(card);
        lv_obj_set_size(start_slab, 120, lv_pct(100));
        lv_obj_align(start_slab, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(start_slab, lv_color_hex(0x09A672), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(start_slab, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(start_slab, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(start_slab, 28, LV_PART_MAIN);

        lv_obj_t * start_lbl = lv_label_create(start_slab);
        lv_label_set_text(start_lbl, "START");
        lv_obj_set_style_text_font(start_lbl, &lv_font_montserrat_24, LV_PART_MAIN);
        lv_obj_set_style_text_color(start_lbl, lv_color_hex(0xF4FBF7), LV_PART_MAIN);
        lv_obj_center(start_lbl);

        lv_obj_t * content = lv_obj_create(card);
        lv_obj_remove_style_all(content);
        lv_obj_set_size(content, lv_pct(66), lv_pct(100));
        lv_obj_align(content, LV_ALIGN_LEFT_MID, 18, 0);

        lv_obj_t * title = lv_label_create(content);
        lv_obj_set_width(title, lv_pct(100));
        lv_obj_set_style_text_font(title, &lv_font_montserrat_48, LV_PART_MAIN);
        lv_obj_set_style_text_color(title, lv_color_hex(0xF3F4F7), LV_PART_MAIN);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 22);

        lv_obj_t * subtitle = lv_label_create(content);
        lv_obj_set_width(subtitle, lv_pct(100));
        lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_24, LV_PART_MAIN);
        lv_obj_set_style_text_color(subtitle, lv_color_hex(0xC3C7CF), LV_PART_MAIN);
        lv_label_set_long_mode(subtitle, LV_LABEL_LONG_DOT);
        lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 0, 108);

        lv_obj_t * time_range = lv_label_create(content);
        lv_obj_set_style_text_font(time_range, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(time_range, lv_color_hex(0xA0C9D5), LV_PART_MAIN);
        lv_obj_align(time_range, LV_ALIGN_BOTTOM_LEFT, 0, -28);

        lv_obj_t * status = lv_label_create(content);
        lv_obj_set_style_text_font(status, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(status, lv_color_hex(0xA8E7B4), LV_PART_MAIN);
        lv_obj_align(status, LV_ALIGN_BOTTOM_LEFT, 0, -10);

        g_cards[i].card = card;
        g_cards[i].title = title;
        g_cards[i].subtitle = subtitle;
        g_cards[i].time_range = time_range;
        g_cards[i].status = status;
    }
}

lv_obj_t * screen_home_create(void) {
    lv_display_t * disp = lv_display_get_default();
    int32_t sw = lv_display_get_horizontal_resolution(disp);
    int32_t sh = lv_display_get_vertical_resolution(disp);

    int32_t margin = clampi(sw / 48, 12, 28);
    int32_t header_h = clampi((sh * 148) / 480, 120, 156);
    int32_t footer_h = 22;
    int32_t task_y = margin + header_h;
    int32_t task_h = sh - task_y - footer_h - margin;
    task_h = clampi(task_h, 250, sh - 110);
    g_card_height = clampi((task_h * 214) / 332, 182, 230);

    lv_obj_t * screen = lv_obj_create(NULL);
    lv_obj_set_size(screen, sw, sh);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0C0C0C), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0x060606), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * time_col = lv_obj_create(screen);
    lv_obj_remove_style_all(time_col);
    int32_t time_w = clampi((sw * 330) / 640, 260, 340);
    int32_t time_x = clampi((sw * 28) / 640, 12, 28);
    int32_t time_y = clampi((sh * 18) / 480, 10, 20);
    lv_obj_set_size(time_col, time_w, header_h);
    lv_obj_set_pos(time_col, time_x, time_y);

    g_lbl_time = lv_label_create(time_col);
    lv_obj_set_style_text_font(g_lbl_time, &Antonio_bold_80, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_lbl_time, lv_color_hex(0xF5F6F8), LV_PART_MAIN);
    lv_obj_align(g_lbl_time, LV_ALIGN_TOP_LEFT, 0, 0);

    g_lbl_date = lv_label_create(time_col);
    lv_obj_set_style_text_font(g_lbl_date, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_lbl_date, lv_color_hex(0xD3D7DD), LV_PART_MAIN);
    lv_obj_align(g_lbl_date, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t * quick_btn = lv_btn_create(screen);
    int32_t quick_w = clampi((sw * 220) / 640, 188, 220);
    int32_t quick_h = clampi((sh * 94) / 480, 80, 96);
    int32_t quick_gap = clampi((sw * 10) / 640, 6, 12);
    int32_t quick_x = time_x + time_w + quick_gap;
    int32_t quick_y = time_y + 4;
    int32_t quick_right_limit = sw - quick_w - clampi((sw * 10) / 640, 8, 12);
    if (quick_x > quick_right_limit) {
        quick_x = quick_right_limit;
    }
    lv_obj_set_size(quick_btn, quick_w, quick_h);
    lv_obj_set_pos(quick_btn, quick_x, quick_y);
    lv_obj_set_style_bg_opa(quick_btn, LV_OPA_10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(quick_btn, lv_color_hex(0x1D1D1D), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(quick_btn, lv_color_hex(0x151515), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(quick_btn, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_border_width(quick_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(quick_btn, lv_color_hex(0x3B4048), LV_PART_MAIN);
    lv_obj_set_style_radius(quick_btn, 18, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(quick_btn, 24, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(quick_btn, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(quick_btn, LV_OPA_30, LV_PART_MAIN);
    lv_obj_add_event_cb(quick_btn, quick_focus_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t * quick_lbl = lv_label_create(quick_btn);
    lv_label_set_text(quick_lbl, LV_SYMBOL_PLAY "  Quick Focus");
    lv_obj_set_style_text_font(quick_lbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(quick_lbl, lv_color_hex(0xDFE3EA), LV_PART_MAIN);
    lv_obj_center(quick_lbl);

    lv_obj_t * layer_back = lv_obj_create(screen);
    lv_obj_set_size(layer_back, sw - clampi((sw * 80) / 640, 42, 80), task_h - 20);
    lv_obj_set_pos(layer_back, clampi((sw * 38) / 640, 20, 38), task_y + 14);
    lv_obj_set_style_bg_color(layer_back, lv_color_hex(0x202935), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(layer_back, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_border_width(layer_back, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(layer_back, 22, LV_PART_MAIN);
    lv_obj_add_flag(layer_back, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t * layer_mid = lv_obj_create(screen);
    lv_obj_set_size(layer_mid, sw - clampi((sw * 60) / 640, 30, 60), task_h - 10);
    lv_obj_set_pos(layer_mid, clampi((sw * 52) / 640, 26, 52), task_y + 8);
    lv_obj_set_style_bg_color(layer_mid, lv_color_hex(0x141920), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(layer_mid, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(layer_mid, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(layer_mid, 22, LV_PART_MAIN);
    lv_obj_add_flag(layer_mid, LV_OBJ_FLAG_HIDDEN);

    g_task_list = lv_obj_create(screen);
    lv_obj_set_size(g_task_list, sw - (margin * 2), task_h);
    lv_obj_set_pos(g_task_list, margin, task_y);
    lv_obj_set_style_bg_opa(g_task_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_task_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(g_task_list, clampi((sw * 40) / 640, 18, 40), LV_PART_MAIN);
    lv_obj_set_style_pad_right(g_task_list, clampi((sw * 40) / 640, 18, 40), LV_PART_MAIN);
    lv_obj_set_style_pad_top(g_task_list, clampi((task_h - g_card_height) / 2, 28, 62), LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(g_task_list, clampi((task_h - g_card_height) / 2, 28, 62), LV_PART_MAIN);
    lv_obj_set_style_pad_gap(g_task_list, clampi((sh * 44) / 480, 24, 44), LV_PART_MAIN);
    lv_obj_set_scroll_dir(g_task_list, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(g_task_list, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(g_task_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_layout(g_task_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(g_task_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_task_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(g_task_list, list_scroll_event, LV_EVENT_SCROLL_END, NULL);

    create_task_cards(g_task_list);

    lv_obj_t * indicator = lv_obj_create(screen);
    lv_obj_remove_style_all(indicator);
    lv_obj_set_size(indicator, 10, 88);
    lv_obj_set_pos(indicator, sw - clampi((sw * 12) / 640, 8, 12), task_y + (task_h / 2) - 44);

    lv_obj_t * dot_top = lv_obj_create(indicator);
    lv_obj_set_size(dot_top, 7, 7);
    lv_obj_set_style_radius(dot_top, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dot_top, lv_color_hex(0xE7EBEF), LV_PART_MAIN);
    lv_obj_set_style_border_width(dot_top, 0, LV_PART_MAIN);
    lv_obj_align(dot_top, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t * bar = lv_obj_create(indicator);
    lv_obj_set_size(bar, 7, 38);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xEDF1F5), LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t * dot_bottom = lv_obj_create(indicator);
    lv_obj_set_size(dot_bottom, 7, 7);
    lv_obj_set_style_radius(dot_bottom, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dot_bottom, lv_color_hex(0xD4DAE0), LV_PART_MAIN);
    lv_obj_set_style_border_width(dot_bottom, 0, LV_PART_MAIN);
    lv_obj_align(dot_bottom, LV_ALIGN_BOTTOM_MID, 0, 0);

    g_lbl_footer = lv_label_create(screen);
    lv_obj_set_style_text_font(g_lbl_footer, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_lbl_footer, lv_color_hex(0x848C99), LV_PART_MAIN);
    lv_obj_set_pos(g_lbl_footer, margin, sh - footer_h);
    lv_label_set_text(g_lbl_footer, "Ready");

    g_lbl_status = g_lbl_footer;
    app_state_set_status("Ready");
    app_state_set_tasks_loading(true);

    update_clock_labels();
    render_task_cards();

    lv_timer_create(clock_timer_cb, 1000, NULL);
    lv_timer_create(refresh_timer_cb, HOME_API_REFRESH_MS, NULL);

    start_due_today_fetch();

    return screen;
}
