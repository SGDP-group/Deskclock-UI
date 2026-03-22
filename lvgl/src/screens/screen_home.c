#include "screen_home.h"
#include "lvgl/lvgl.h"
#include "../data/app_state.h"
#include "../components/session_confirm_popup.h"
#include "../ui.h"
#include "src/home_api_client.h"
#include "src/home_config.h"

#include <ctype.h>
#include <string.h>
#include <time.h>

/* -----------------------------------------------------------------------
 * Fonts
 * ----------------------------------------------------------------------- */
extern const lv_font_t Antonio_bold_80;

/* -----------------------------------------------------------------------
 * Design tokens — mirroring the React CSS variables
 *   --bg:               #090909
 *   --surface:          #1a1a1a
 *   --accent:           #09a672
 *   --accent-hover:     #13b982
 *   --text-primary:     #ffffff
 *   --text-secondary:   #b2b2b2
 *   --text-muted:       #555555
 *   --border:           rgba(255,255,255,0.08)
 * ----------------------------------------------------------------------- */
#define CLR_BG_TOP          0x181818
#define CLR_BG_BOTTOM       0x060606
#define CLR_SURFACE         0x1A1A1A
#define CLR_SURFACE_BTN_TOP 0x1D1D1D
#define CLR_SURFACE_BTN_BOT 0x151515
#define CLR_ACCENT          0x09A672
#define CLR_ACCENT_STRIP    0x10B981
#define CLR_TEXT_PRIMARY     0xFFFFFF
#define CLR_TEXT_CLOCK       0xF5F6F8
#define CLR_TEXT_DATE        0xF1F1F1
#define CLR_TEXT_SECONDARY   0xD7DBE0
#define CLR_TEXT_DESC        0xD2D2D2
#define CLR_TEXT_MUTED       0x848C99
#define CLR_BORDER_SUBTLE    0x3B4048
#define CLR_TIME_BADGE_BG    0xFFFFFF
#define CLR_TIME_BADGE_TEXT  0x111111
#define CLR_DOT_INACTIVE     0x6B6B6B
#define CLR_DOT_ACTIVE       0xFFFFFF

/* -----------------------------------------------------------------------
 * Layout constants — fixed 800x480 display
 * ----------------------------------------------------------------------- */
#define SCREEN_W           800
#define SCREEN_H           480

/* Header region */
#define HEADER_H           160
#define HEADER_PAD_TOP     16
#define HEADER_PAD_LEFT    24
#define HEADER_PAD_RIGHT   24

/* Clock */
#define CLOCK_LETTER_SPACE 16
#define DATE_FONT_SIZE     48   /* mapped to lv_font_montserrat_48 */

/* Quick Focus button */
#define QF_BTN_W           300
#define QF_BTN_H           120
#define QF_BTN_RADIUS      26

/* Task carousel area */
#define TASK_AREA_PAD_X    24
#define TASK_CARD_H        210
#define TASK_CARD_GAP      18
#define TASK_CARD_RADIUS   32

/* Card internals */
#define CARD_ACCENT_W      16
#define CARD_BODY_PAD_L    20
#define CARD_BODY_PAD_T    24
#define CARD_START_BTN_W   190
#define CARD_START_BTN_R   32

/* Time badge inside card */
#define TIME_BADGE_H       36
#define TIME_BADGE_RADIUS  8
#define TIME_BADGE_PAD_X   12

/* Carousel indicator dots */
#define DOT_SIZE           8
#define DOT_ACTIVE_H       40
#define DOT_RIGHT_MARGIN   14
#define DOT_GAP            12

/* Footer */
#define FOOTER_H           28

/* -----------------------------------------------------------------------
 * Task card pool
 * ----------------------------------------------------------------------- */
typedef struct {
    lv_obj_t * card;
    lv_obj_t * title;
    lv_obj_t * time_badge;
    lv_obj_t * subtitle;
    lv_obj_t * status;
} TaskCardRefs;

#define HOME_CARD_POOL_SIZE 6

static lv_obj_t * g_lbl_time     = NULL;
static lv_obj_t * g_lbl_date     = NULL;
static lv_obj_t * g_lbl_footer   = NULL;
static lv_obj_t * g_task_list    = NULL;
static TaskCardRefs g_cards[HOME_CARD_POOL_SIZE];
static lv_obj_t * g_lbl_empty_state = NULL;

static bool g_fetch_inflight = false;
static char g_last_time[16]  = {0};
static char g_last_date[24]  = {0};

/* -----------------------------------------------------------------------
 * Utility helpers
 * ----------------------------------------------------------------------- */
static void copy_text_safe(char * dst, size_t dst_len, const char * src) {
    if (dst == NULL || dst_len == 0) return;
    if (src == NULL) { dst[0] = '\0'; return; }
    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

static void uppercase_ascii(char * text) {
    if (text == NULL) return;
    for (size_t i = 0; text[i] != '\0'; i++) {
        text[i] = (char)toupper((unsigned char)text[i]);
    }
}

/* -----------------------------------------------------------------------
 * Clock logic
 * ----------------------------------------------------------------------- */
static void update_clock_labels(void) {
    if (g_lbl_time == NULL || g_lbl_date == NULL) return;

    time_t now = time(NULL);
    struct tm local_tm;
#ifdef _WIN32
    localtime_s(&local_tm, &now);
#else
    localtime_r(&now, &local_tm);
#endif

    char time_buf[16] = {0};
    char date_buf[24] = {0};

    strftime(time_buf, sizeof(time_buf), "%H : %M", &local_tm);
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

static void clock_timer_cb(lv_timer_t * timer) {
    (void)timer;
    update_clock_labels();
}

/* -----------------------------------------------------------------------
 * Task card rendering
 * ----------------------------------------------------------------------- */
static void render_empty_state(const char * text) {
    if (g_task_list != NULL) {
        lv_obj_add_flag(g_task_list, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_lbl_empty_state != NULL) {
        lv_label_set_text(g_lbl_empty_state, text);
        lv_obj_clear_flag(g_lbl_empty_state, LV_OBJ_FLAG_HIDDEN);
    }
}

static void render_task_cards(void) {
    if (g_app_state.tasks_loading) {
        render_empty_state("Loading tasks...");
        return;
    }
    if (g_app_state.home_task_count == 0) {
        render_empty_state("No tasks today, Enjoy your day :D");
        return;
    }
    
    /* Ensure list is visible and empty state text is hidden */
    if (g_task_list != NULL) {
        lv_obj_clear_flag(g_task_list, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_lbl_empty_state != NULL) {
        lv_obj_add_flag(g_lbl_empty_state, LV_OBJ_FLAG_HIDDEN);
    }

    uint8_t visible = g_app_state.home_task_count;
    if (visible > HOME_CARD_POOL_SIZE) visible = HOME_CARD_POOL_SIZE;

    for (uint8_t i = 0; i < HOME_CARD_POOL_SIZE; i++) {
        lv_obj_t * card = g_cards[i].card;
        if (card == NULL) continue;

        if (i >= visible) {
            lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        const HomeTask * task = &g_app_state.home_tasks[i];
        lv_label_set_text(g_cards[i].title,      task->title);
        lv_label_set_text(g_cards[i].subtitle,    task->subtitle);
        lv_label_set_text(g_cards[i].time_badge,  task->time_range);
        lv_label_set_text(g_cards[i].status,      task->status);

        lv_obj_set_style_text_color(
            g_cards[i].status,
            task->completed ? lv_color_hex(0x8EF2A5) : lv_color_hex(0xD8DEE9),
            LV_PART_MAIN);

        lv_obj_clear_flag(card, LV_OBJ_FLAG_HIDDEN);
    }
}

/* -----------------------------------------------------------------------
 * API fetch
 * ----------------------------------------------------------------------- */
static void fetch_due_today_now(void) {
    HomeApiTask api_tasks[APP_MAX_HOME_TASKS];
    HomeTask    ui_tasks[APP_MAX_HOME_TASKS];
    memset(api_tasks, 0, sizeof(api_tasks));
    memset(ui_tasks,  0, sizeof(ui_tasks));

    uint8_t count = 0;
    bool ok = home_api_fetch_due_today(api_tasks, &count, HOME_CARD_POOL_SIZE);
    if (ok) {
        for (uint8_t i = 0; i < count; i++) {
            ui_tasks[i].id        = api_tasks[i].id;
            ui_tasks[i].completed = api_tasks[i].completed;
            ui_tasks[i].duration_minutes = api_tasks[i].duration_minutes;
            copy_text_safe(ui_tasks[i].title,      sizeof(ui_tasks[i].title),      api_tasks[i].title);
            copy_text_safe(ui_tasks[i].subtitle,   sizeof(ui_tasks[i].subtitle),   api_tasks[i].subtitle);
            copy_text_safe(ui_tasks[i].time_range, sizeof(ui_tasks[i].time_range), api_tasks[i].time_range);
            copy_text_safe(ui_tasks[i].status,     sizeof(ui_tasks[i].status),     api_tasks[i].status);
        }
    }

    app_state_set_tasks_loading(false);
    if (ok) {
        app_state_set_home_tasks(ui_tasks, count);
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
    if (g_fetch_inflight) return;
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

static void start_session_from_popup(SessionConfirmKind kind, uint8_t task_index) {
    if (kind == SESSION_CONFIRM_KIND_QUICK) {
        ui_navigate_focus_session("Quick Session", (uint32_t)HOME_QUICK_SESSION_MINUTES * 60U, true, -1);
        return;
    }

    if (task_index >= g_app_state.home_task_count) return;

    const HomeTask * task = &g_app_state.home_tasks[task_index];
    const char * task_title = (task->subtitle[0] != '\0') ? task->subtitle : task->title;
    uint32_t minutes = (task->duration_minutes > 0) ? (uint32_t)task->duration_minutes : (uint32_t)HOME_TASK_FALLBACK_MINUTES;

    ui_navigate_focus_session(task_title, minutes * 60U, false, task->id);
}

static void quick_focus_event(lv_event_t * e) {
    (void)e;
    app_state_set_status("Quick Focus ready");
    session_confirm_popup_show_quick();
    if (g_lbl_footer != NULL) {
        lv_label_set_text(g_lbl_footer, g_app_state.status_message);
    }
}

static void task_start_event(lv_event_t * e) {
    uint32_t idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    if (idx >= g_app_state.home_task_count) return;

    const HomeTask * task = &g_app_state.home_tasks[idx];
    const char * subtask = (task->subtitle[0] != '\0') ? task->subtitle : task->title;

    session_confirm_popup_show_task(subtask, (uint8_t)idx);
}

static void create_single_task_card(lv_obj_t * parent, uint8_t idx) {
    /* Card container */
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, TASK_CARD_H);
    lv_obj_set_style_bg_color(card, lv_color_hex(CLR_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(card, TASK_CARD_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(card, 16, LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(card, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(card, LV_OPA_40, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Green accent strip (left edge) */
    lv_obj_t * accent = lv_obj_create(card);
    lv_obj_set_size(accent, CARD_ACCENT_W, lv_pct(100));
    lv_obj_align(accent, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(accent, lv_color_hex(CLR_ACCENT_STRIP), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(accent, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(accent, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE);

    /* START button (right side, full height, rounded) */
    lv_obj_t * start_slab = lv_obj_create(card);
    lv_obj_set_size(start_slab, CARD_START_BTN_W, lv_pct(100));
    lv_obj_align(start_slab, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(start_slab, lv_color_hex(CLR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(start_slab, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(start_slab, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(start_slab, CARD_START_BTN_R, LV_PART_MAIN);
    lv_obj_clear_flag(start_slab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(start_slab, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(start_slab, task_start_event, LV_EVENT_CLICKED, (void *)(uintptr_t)idx);

    lv_obj_t * start_lbl = lv_label_create(start_slab);
    lv_label_set_text(start_lbl, "START");
    lv_obj_set_style_text_font(start_lbl, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(start_lbl, lv_color_hex(CLR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(start_lbl, 4, LV_PART_MAIN);
    lv_obj_center(start_lbl);

    /* Body content area (between accent strip and START button) */
    lv_obj_t * body = lv_obj_create(card);
    lv_obj_remove_style_all(body);
    int32_t body_w = SCREEN_W - (TASK_AREA_PAD_X * 2) - CARD_ACCENT_W - CARD_START_BTN_W - CARD_BODY_PAD_L;
    lv_obj_set_size(body, body_w, lv_pct(100));
    lv_obj_align(body, LV_ALIGN_LEFT_MID, CARD_ACCENT_W + CARD_BODY_PAD_L, 0);

    /* Title — large, bold, truncated */
    lv_obj_t * title = lv_label_create(body);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, CARD_BODY_PAD_T);

    /* White time badge — full width bar with time range text */
    lv_obj_t * badge_cont = lv_obj_create(body);
    lv_obj_set_width(badge_cont, lv_pct(95));
    lv_obj_set_height(badge_cont, TIME_BADGE_H);
    lv_obj_set_style_bg_color(badge_cont, lv_color_hex(CLR_TIME_BADGE_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(badge_cont, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(badge_cont, TIME_BADGE_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_border_width(badge_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(badge_cont, TIME_BADGE_PAD_X, LV_PART_MAIN);
    lv_obj_set_style_pad_right(badge_cont, TIME_BADGE_PAD_X, LV_PART_MAIN);
    lv_obj_align(badge_cont, LV_ALIGN_TOP_LEFT, 0, CARD_BODY_PAD_T + 74);
    lv_obj_clear_flag(badge_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * time_lbl = lv_label_create(badge_cont);
    lv_obj_set_style_text_font(time_lbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(time_lbl, lv_color_hex(CLR_TIME_BADGE_TEXT), LV_PART_MAIN);
    lv_obj_align(time_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    /* Subtitle / description */
    lv_obj_t * subtitle = lv_label_create(body);
    lv_obj_set_width(subtitle, lv_pct(100));
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(CLR_TEXT_DESC), LV_PART_MAIN);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_DOT);
    lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 0, CARD_BODY_PAD_T + 122);

    /* Status label (bottom of body) */
    lv_obj_t * status = lv_label_create(body);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(status, lv_color_hex(0xA8E7B4), LV_PART_MAIN);
    lv_obj_align(status, LV_ALIGN_BOTTOM_LEFT, 0, -14);

    /* Store references */
    g_cards[idx].card       = card;
    g_cards[idx].title      = title;
    g_cards[idx].time_badge = time_lbl;
    g_cards[idx].subtitle   = subtitle;
    g_cards[idx].status     = status;
}

static void create_task_cards(lv_obj_t * parent) {
    for (uint8_t i = 0; i < HOME_CARD_POOL_SIZE; i++) {
        create_single_task_card(parent, i);
    }
}

/* -----------------------------------------------------------------------
 * Native LVGL Scrollbar Styling
 * (Replaced static carousel indicator)
 * ----------------------------------------------------------------------- */
static void apply_list_scrollbar_style(lv_obj_t * list) {
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(list, lv_color_hex(CLR_DOT_ACTIVE), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list, LV_OPA_50, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(list, 10, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(list, 8, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(list, 8, LV_PART_SCROLLBAR);
}

/* -----------------------------------------------------------------------
 * screen_home_create — main entry point
 * Reconstructs the React Dashboard layout at 800x480:
 *   ┌─────────────────────────────────────────┐
 *   │  HH : MM          [ ▶ Quick Focus ]     │  ← header
 *   │  SUN, 22 MAR                            │
 *   ├─────────────────────────────────────────┤
 *   │  ┌─────────────────────────────────┐  · │  ← task carousel
 *   │  │ [▌] Title            [ START ]  │  █ │
 *   │  │     ┌─ 09:00 – 10:30 ──────┐   │  · │
 *   │  │     Description             │   │    │
 *   │  └─────────────────────────────────┘    │
 *   ├─────────────────────────────────────────┤
 *   │  Ready                                  │  ← footer
 *   └─────────────────────────────────────────┘
 * ----------------------------------------------------------------------- */
lv_obj_t * screen_home_create(void) {
    int32_t sw = SCREEN_W;
    int32_t sh = SCREEN_H;

    /* Task area geometry */
    int32_t task_y = HEADER_H;
    int32_t task_h = sh - HEADER_H - FOOTER_H;

    /* ── Screen ── */
    lv_obj_t * screen = lv_obj_create(NULL);
    lv_obj_set_size(screen, sw, sh);
    lv_obj_set_style_bg_color(screen, lv_color_hex(CLR_BG_TOP), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(CLR_BG_BOTTOM), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Clock column (top-left) ── */
    lv_obj_t * time_col = lv_obj_create(screen);
    lv_obj_remove_style_all(time_col);
    lv_obj_set_size(time_col, 440, HEADER_H - HEADER_PAD_TOP);
    lv_obj_set_pos(time_col, HEADER_PAD_LEFT, HEADER_PAD_TOP);

    g_lbl_time = lv_label_create(time_col);
    lv_obj_set_width(g_lbl_time, 440); /* Keep large clock font visible without overrun on 800px screen */
    lv_obj_set_style_text_font(g_lbl_time, &Antonio_bold_80, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_lbl_time, lv_color_hex(CLR_TEXT_CLOCK), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(g_lbl_time, CLOCK_LETTER_SPACE, LV_PART_MAIN);
    lv_obj_align(g_lbl_time, LV_ALIGN_TOP_LEFT, 0, 0);

    g_lbl_date = lv_label_create(time_col);
    lv_obj_set_style_text_font(g_lbl_date, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_lbl_date, lv_color_hex(CLR_TEXT_DATE), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(g_lbl_date, 7, LV_PART_MAIN);
    lv_obj_align_to(g_lbl_date, g_lbl_time, LV_ALIGN_OUT_BOTTOM_LEFT, 2, 8);

    /* ── Quick Focus button (top-right) ── */
    lv_obj_t * quick_btn = lv_btn_create(screen);
    lv_obj_set_size(quick_btn, QF_BTN_W, QF_BTN_H);
    lv_obj_set_pos(quick_btn, sw - QF_BTN_W - HEADER_PAD_RIGHT, HEADER_PAD_TOP);
    lv_obj_set_style_bg_color(quick_btn, lv_color_hex(CLR_SURFACE_BTN_TOP), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(quick_btn, lv_color_hex(CLR_SURFACE_BTN_BOT), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(quick_btn, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_border_width(quick_btn, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(quick_btn, lv_color_hex(CLR_BORDER_SUBTLE), LV_PART_MAIN);
    lv_obj_set_style_radius(quick_btn, QF_BTN_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(quick_btn, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(quick_btn, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(quick_btn, LV_OPA_30, LV_PART_MAIN);
    lv_obj_add_event_cb(quick_btn, quick_focus_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t * quick_lbl = lv_label_create(quick_btn);
    lv_label_set_text(quick_lbl, LV_SYMBOL_PLAY "  Quick Focus");
    lv_obj_set_style_text_font(quick_lbl, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(quick_lbl, lv_color_hex(CLR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_center(quick_lbl);

    /* ── Task list (flex column, scrollable) ── */
    g_task_list = lv_obj_create(screen);
    lv_obj_set_size(g_task_list, sw - (TASK_AREA_PAD_X * 2) - DOT_RIGHT_MARGIN, task_h);
    lv_obj_set_pos(g_task_list, TASK_AREA_PAD_X, task_y);
    lv_obj_set_style_bg_opa(g_task_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_task_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(g_task_list, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(g_task_list, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_left(g_task_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(g_task_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(g_task_list, TASK_CARD_GAP, LV_PART_MAIN);
    lv_obj_set_scroll_dir(g_task_list, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(g_task_list, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_layout(g_task_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(g_task_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_task_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    apply_list_scrollbar_style(g_task_list);

    /* Empty state label */
    g_lbl_empty_state = lv_label_create(screen);
    lv_obj_set_style_text_font(g_lbl_empty_state, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_lbl_empty_state, lv_color_hex(CLR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_set_style_text_align(g_lbl_empty_state, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(g_lbl_empty_state, LV_ALIGN_CENTER, 0, 40);
    lv_obj_add_flag(g_lbl_empty_state, LV_OBJ_FLAG_HIDDEN);

    create_task_cards(g_task_list);

    /* ── Footer status label ── */
    g_lbl_footer = lv_label_create(screen);
    lv_obj_set_style_text_font(g_lbl_footer, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_lbl_footer, lv_color_hex(CLR_TEXT_MUTED), LV_PART_MAIN);
    lv_obj_set_pos(g_lbl_footer, HEADER_PAD_LEFT, sh - FOOTER_H);
    lv_label_set_text(g_lbl_footer, "Ready");

    /* ── Wire up global state ── */
    g_lbl_status = g_lbl_footer;
    app_state_set_status("Ready");
    app_state_set_tasks_loading(true);
    session_confirm_popup_set_start_cb(start_session_from_popup);

    /* ── Kick off timers and initial fetch ── */
    update_clock_labels();
    render_task_cards();

    lv_timer_create(clock_timer_cb, 1000, NULL);
    lv_timer_create(refresh_timer_cb, HOME_API_REFRESH_MS, NULL);

    start_due_today_fetch();

    return screen;
}
