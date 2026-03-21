#ifndef HOME_API_CLIENT_H
#define HOME_API_CLIENT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define HOME_API_TASK_TITLE_LEN 64
#define HOME_API_TASK_SUBTITLE_LEN 96
#define HOME_API_TASK_TIME_RANGE_LEN 24
#define HOME_API_TASK_STATUS_LEN 20

typedef struct {
    int id;
    bool completed;
    char title[HOME_API_TASK_TITLE_LEN];
    char subtitle[HOME_API_TASK_SUBTITLE_LEN];
    char time_range[HOME_API_TASK_TIME_RANGE_LEN];
    char status[HOME_API_TASK_STATUS_LEN];
} HomeApiTask;

bool home_api_fetch_due_today(HomeApiTask * tasks, uint8_t * out_count, uint8_t cap);

#endif /* HOME_API_CLIENT_H */
