#ifndef SCREEN_FOCUS_SESSION_H
#define SCREEN_FOCUS_SESSION_H

#include "lvgl/lvgl.h"
#include <stdint.h>
#include <stdbool.h>

/* Creates a focus session screen with countdown and controls. */
lv_obj_t * screen_focus_session_create(const char * title, uint32_t total_seconds, bool is_quick_session, int task_id);

/* True while a previous stop cleanup is still stopping camera/stream workers. */
bool screen_focus_session_cleanup_inflight(void);

#endif /* SCREEN_FOCUS_SESSION_H */
