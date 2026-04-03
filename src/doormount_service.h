#ifndef DOORMOUNT_SERVICE_H
#define DOORMOUNT_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DOORMOUNT_MAX_NETWORKS 12
#define DOORMOUNT_SSID_MAX_LEN 64

typedef struct {
    uint8_t count;
    char ssids[DOORMOUNT_MAX_NETWORKS][DOORMOUNT_SSID_MAX_LEN];
} DoormountNetworkList;

typedef enum {
    DOORMOUNT_LED_GREEN = 0,
    DOORMOUNT_LED_YELLOW = 1,
    DOORMOUNT_LED_RED = 2
} DoormountLedState;

bool doormount_service_scan(DoormountNetworkList * out_list, char * error, size_t error_len);
bool doormount_service_setup_selected(const char * doormount_ssid, char * error, size_t error_len);
bool doormount_service_set_led_state(DoormountLedState state, char * error, size_t error_len);

#endif /* DOORMOUNT_SERVICE_H */
