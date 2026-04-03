#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#include <stdbool.h>

#define DEVICE_CONFIG_WIFI_MAX_LEN 64

typedef struct DeviceConfig {
    bool provisioned;
    int user_id;
    char wifi_ssid[DEVICE_CONFIG_WIFI_MAX_LEN];
    char wifi_password[DEVICE_CONFIG_WIFI_MAX_LEN];
} DeviceConfig;

bool device_config_load(void);
bool device_config_save(const DeviceConfig * config);
void device_config_set_defaults(void);
const DeviceConfig * device_config_get(void);
int device_config_get_user_id(void);
bool device_config_is_provisioned(void);
bool device_config_has_wifi_credentials(void);
bool device_config_factory_reset(void);

#endif /* DEVICE_CONFIG_H */
