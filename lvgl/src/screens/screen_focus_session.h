#ifndef SCREEN_FOCUS_SESSION_H
#define SCREEN_FOCUS_SESSION_H

#include "lvgl/lvgl.h"
#include <stdint.h>

/* Creates a focus session screen with countdown and controls. */
lv_obj_t * screen_focus_session_create(const char * title, uint32_t total_seconds);

#endif /* SCREEN_FOCUS_SESSION_H */
