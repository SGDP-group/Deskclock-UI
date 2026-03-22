#ifndef HOME_CONFIG_H
#define HOME_CONFIG_H

/* Central home screen API config. */
#define HOME_API_HOST "127.0.0.1"
#define HOME_API_PORT 8080
#define HOME_DEVICE_IP HOME_API_HOST
#define HOME_API_USER_ID 3
/* Set HOME_API_USER_ID to 0 to trigger the auth token flow. */

#define HOME_API_REFRESH_MS 180000
#define HOME_API_PENDING_POLL_MS 250

#endif /* HOME_CONFIG_H */
