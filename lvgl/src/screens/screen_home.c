/**
 * screen_home.c — Home screen
 * React analogy: pages/Home.jsx
 *
 * This is a starter example screen with:
 *  - A title label
 *  - A counter button (demonstrates state + events)
 *  - A status label (demonstrates updating UI from state)
 *
 * Replace the contents of screen_home_create() with your actual UI.
 */

#include "screen_home.h"
#include "lvgl/lvgl.h"
#include "../styles/theme.h"
#include "../data/app_state.h"

/* -----------------------------------------------------------------------
 * Local state — like useState() inside a React component
 * ----------------------------------------------------------------------- */
static int click_count = 0;

/* -----------------------------------------------------------------------
 * Event handlers — like onClick, onChange etc.
 * ----------------------------------------------------------------------- */

static void btn_click_handler(lv_event_t * e) {
    /* lv_event_get_target() = event.target in JS */
    lv_obj_t * btn   = lv_event_get_target(e);
    lv_obj_t * label = lv_obj_get_child(btn, 0);

    click_count++;

    /* lv_label_set_text_fmt = JS template literal: `Clicked ${n} times` */
    lv_label_set_text_fmt(label, "Clicked %d times", click_count);
}

/* -----------------------------------------------------------------------
 * Component — like export default function HomeScreen()
 * ----------------------------------------------------------------------- */

lv_obj_t * screen_home_create(void) {

    /* Create a blank screen — like a full-viewport <div> */
    lv_obj_t * screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1A1A2E), LV_PART_MAIN);

    /* ------------------------------------------------------------------
     * Title label
     * ------------------------------------------------------------------ */
    lv_obj_t * title = lv_label_create(screen);
    lv_label_set_text(title, "Deskclock UI");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00FFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    /* ------------------------------------------------------------------
     * Counter button — demonstrates touch events
     * ------------------------------------------------------------------ */
    lv_obj_t * btn = lv_btn_create(screen);
    lv_obj_set_size(btn, 220, 55);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);

    /* Apply the primary button style from theme.c */
    apply_primary_btn_style(btn);

    /* Buttons don't have built-in text — add a child label */
    /* Like: <button><span>Click Me</span></button>         */
    lv_obj_t * btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Tap Me");
    lv_obj_center(btn_label);

    /* Attach event handler — like onClick={btn_click_handler} */
    lv_obj_add_event_cb(btn, btn_click_handler, LV_EVENT_CLICKED, NULL);

    /* ------------------------------------------------------------------
     * Status label — demonstrates updating UI from global state
     * Register it in app_state so other files can update it too
     * ------------------------------------------------------------------ */
    lv_obj_t * status_lbl = lv_label_create(screen);
    lv_label_set_text(status_lbl, "Ready");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
    lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(status_lbl, LV_ALIGN_BOTTOM_MID, 0, -20);

    /* Store the pointer so app_state.c can update this label from anywhere */
    g_lbl_status = status_lbl;

    return screen;
}
