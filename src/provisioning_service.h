#ifndef PROVISIONING_SERVICE_H
#define PROVISIONING_SERVICE_H

#include <stdbool.h>

bool provisioning_service_start_if_needed(void);
bool provisioning_service_restart_for_reprovision(void);
void provisioning_service_stop(void);
bool provisioning_service_is_running(void);
const char * provisioning_service_get_softap_ssid(void);

#endif /* PROVISIONING_SERVICE_H */
