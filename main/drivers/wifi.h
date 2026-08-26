#pragma once

#include "esp_err.h"
#include "esp_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char ssid[33];
  int8_t rssi;
  wifi_auth_mode_t authmode;
} wifi_scan_entry_t;

void wifi_manager_init(void);
uint16_t wifi_manager_scan(wifi_scan_entry_t **out_list);
esp_err_t wifi_manager_connect(const char *ssid, const char *password);
bool wifi_manager_is_connected(void);

// Persistent Storage APIs
void wifi_save_credentials(const char *ssid, const char *password);
void wifi_auto_connect_saved(void);

#ifdef __cplusplus
}
#endif