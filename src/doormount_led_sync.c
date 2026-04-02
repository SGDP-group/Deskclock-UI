#include "doormount_led_sync.h"

#include "doormount_service.h"
#include "lvgl/src/data/app_state.h"

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define DOORMOUNT_LED_YELLOW_WINDOW_MINUTES 10
#define DOORMOUNT_LED_RETRY_MIN_SECONDS 2U
#define DOORMOUNT_LED_RETRY_MAX_SECONDS 60U

static bool s_focus_active = false;
static bool s_has_last_sent = false;
static DoormountLedState s_last_sent_state = DOORMOUNT_LED_GREEN;
static bool s_retry_pending = false;
static DoormountLedState s_retry_state = DOORMOUNT_LED_GREEN;
static time_t s_next_retry_epoch = 0;
static uint32_t s_retry_delay_seconds = DOORMOUNT_LED_RETRY_MIN_SECONDS;
static time_t s_last_eval_epoch = 0;

static bool parse_start_minutes(const char * time_range, int * out_minutes) {
    int hour = -1;
    int minute = -1;

    if (time_range == NULL || out_minutes == NULL) {
        return false;
    }

    if (sscanf(time_range, "%2d:%2d", &hour, &minute) != 2) {
        return false;
    }

    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return false;
    }

    *out_minutes = (hour * 60) + minute;
    return true;
}

static bool has_subtask_in_yellow_window(const struct tm * local_now) {
    if (local_now == NULL) {
        return false;
    }

    const int now_minutes = (local_now->tm_hour * 60) + local_now->tm_min;

    for (uint8_t i = 0; i < g_app_state.home_task_count && i < APP_MAX_HOME_TASKS; i++) {
        const HomeTask * task = &g_app_state.home_tasks[i];
        int start_minutes = 0;

        if (task->completed) {
            continue;
        }

        if (!parse_start_minutes(task->time_range, &start_minutes)) {
            continue;
        }

        const int minutes_until_start = start_minutes - now_minutes;
        if (minutes_until_start >= 0 && minutes_until_start <= DOORMOUNT_LED_YELLOW_WINDOW_MINUTES) {
            return true;
        }
    }

    return false;
}

static DoormountLedState compute_desired_state(time_t now_epoch) {
    if (s_focus_active) {
        return DOORMOUNT_LED_RED;
    }

    struct tm local_now;
    bool time_ok = false;

#ifdef _WIN32
    time_ok = (localtime_s(&local_now, &now_epoch) == 0);
#else
    time_ok = (localtime_r(&now_epoch, &local_now) != NULL);
#endif

    if (time_ok && has_subtask_in_yellow_window(&local_now)) {
        return DOORMOUNT_LED_YELLOW;
    }

    return DOORMOUNT_LED_GREEN;
}

static void schedule_retry(time_t now_epoch, DoormountLedState desired_state) {
    s_retry_pending = true;
    s_retry_state = desired_state;

    if (s_retry_delay_seconds < DOORMOUNT_LED_RETRY_MIN_SECONDS) {
        s_retry_delay_seconds = DOORMOUNT_LED_RETRY_MIN_SECONDS;
    }

    s_next_retry_epoch = now_epoch + (time_t)s_retry_delay_seconds;

    if (s_retry_delay_seconds < DOORMOUNT_LED_RETRY_MAX_SECONDS) {
        s_retry_delay_seconds *= 2U;
        if (s_retry_delay_seconds > DOORMOUNT_LED_RETRY_MAX_SECONDS) {
            s_retry_delay_seconds = DOORMOUNT_LED_RETRY_MAX_SECONDS;
        }
    }
}

static bool push_led_state(DoormountLedState state) {
    char error[160] = {0};
    return doormount_service_set_led_state(state, error, sizeof(error));
}

void doormount_led_sync_set_focus_active(bool active) {
    if (s_focus_active == active) {
        return;
    }

    s_focus_active = active;
    s_last_eval_epoch = 0;
}

void doormount_led_sync_tick(void) {
    time_t now_epoch = time(NULL);
    if (now_epoch <= 0) {
        return;
    }

    if (s_last_eval_epoch == now_epoch) {
        return;
    }
    s_last_eval_epoch = now_epoch;

    DoormountLedState desired_state = compute_desired_state(now_epoch);

    if (s_retry_pending) {
        if (desired_state != s_retry_state) {
            s_retry_pending = false;
            s_retry_delay_seconds = DOORMOUNT_LED_RETRY_MIN_SECONDS;
        } else if (now_epoch < s_next_retry_epoch) {
            return;
        }
    }

    if (s_has_last_sent && !s_retry_pending && desired_state == s_last_sent_state) {
        return;
    }

    if (push_led_state(desired_state)) {
        s_has_last_sent = true;
        s_last_sent_state = desired_state;
        s_retry_pending = false;
        s_retry_delay_seconds = DOORMOUNT_LED_RETRY_MIN_SECONDS;
        return;
    }

    schedule_retry(now_epoch, desired_state);
}
