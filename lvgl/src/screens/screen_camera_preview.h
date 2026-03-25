#ifndef SCREEN_CAMERA_PREVIEW_H
#define SCREEN_CAMERA_PREVIEW_H

#include "lvgl/lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/* Creates pre-session camera preview screen with Go Back / Start actions. */
lv_obj_t * screen_camera_preview_create(const char * title, uint32_t duration_seconds, bool is_quick_session, int task_id);

#endif /* SCREEN_CAMERA_PREVIEW_H */
