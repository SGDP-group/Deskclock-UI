#ifndef UI_H
#define UI_H

#include <stdint.h>
#include <stdbool.h>

/**
 * ui.h — Root UI initializer
 * React analogy: this is your App.jsx
 */

void ui_init(void);

/* Show setup instructions while SoftAP provisioning is active. */
void ui_show_provisioning_screen(const char * ssid);

/* Navigate from home to an active focus session screen. */
void ui_navigate_focus_session(const char * title, uint32_t duration_seconds, bool is_quick_session, int task_id);

/* Navigate to camera preview before starting focus session. */
void ui_navigate_camera_preview(const char * title, uint32_t duration_seconds, bool is_quick_session, int task_id);

/* Return to a fresh home screen instance. */
void ui_navigate_home(void);

#endif /* UI_H */
