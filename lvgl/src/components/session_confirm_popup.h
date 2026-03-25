#ifndef SESSION_CONFIRM_POPUP_H
#define SESSION_CONFIRM_POPUP_H

#include "lvgl/lvgl.h"

/* Shows the quick-focus confirmation popup. */
void session_confirm_popup_show_quick(void);

/* Shows the task-start confirmation popup with dynamic subtask text. */
void session_confirm_popup_show_task(const char * subtask_name);

/* Closes the popup with exit animation if it is visible. */
void session_confirm_popup_close(void);

#endif /* SESSION_CONFIRM_POPUP_H */
