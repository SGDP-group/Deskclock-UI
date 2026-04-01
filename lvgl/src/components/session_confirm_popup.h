#ifndef SESSION_CONFIRM_POPUP_H
#define SESSION_CONFIRM_POPUP_H

#include "lvgl/lvgl.h"

#include <stdint.h>

typedef enum {
	SESSION_CONFIRM_KIND_QUICK = 0,
	SESSION_CONFIRM_KIND_TASK = 1,
} SessionConfirmKind;

typedef void (*session_confirm_start_cb_t)(SessionConfirmKind kind, uint8_t task_index);

/* Sets callback fired when popup Start button is pressed. */
void session_confirm_popup_set_start_cb(session_confirm_start_cb_t cb);

/* Shows the quick-focus confirmation popup. */
void session_confirm_popup_show_quick(void);

/* Shows the task-start confirmation popup with dynamic task title text. */
void session_confirm_popup_show_task(const char * task_title, uint8_t task_index);

/* Closes the popup with exit animation if it is visible. */
void session_confirm_popup_close(void);

#endif /* SESSION_CONFIRM_POPUP_H */
