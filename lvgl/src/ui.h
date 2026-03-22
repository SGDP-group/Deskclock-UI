#ifndef UI_H
#define UI_H

#include <stdint.h>
#include <stdbool.h>

/**
 * ui.h — Root UI initializer
 * React analogy: this is your App.jsx
 */

void ui_init(void);

/* Navigate from home to an active focus session screen. */
void ui_navigate_focus_session(const char * title, uint32_t duration_seconds, bool is_quick_session);

/* Return to a fresh home screen instance. */
void ui_navigate_home(void);

#endif /* UI_H */
