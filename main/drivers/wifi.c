#include "wifi.h"
#include "cJSON.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>


#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "drivers/ota_engine.h"

#include "wm.h"

static const char *TAG = "WIFI_MGR";
#define WIFI_CONNECTED_BIT BIT0
#define NVS_NAMESPACE "wifi_store"

static EventGroupHandle_t s_wifi_event_group;

static int s_retry_num = 0;
#define MAX_RETRY 5

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

    if (s_retry_num < MAX_RETRY) {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGI(TAG, "Retrying Wi-Fi connection (%d/%d)...", s_retry_num,
               MAX_RETRY);
    } else {
      ESP_LOGW(TAG, "Wi-Fi connection failed after max retries.");
    }
    wm_update_wifi_status(false);
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    s_retry_num = 0; // Reset retry counter on success
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    wm_update_wifi_status(true);

    xTaskCreatePinnedToCore(ota_check_and_download_task, "ota_task", 8192, NULL,
                            5, NULL, 0);
  }
}

void wifi_manager_init(void) {
  s_wifi_event_group = xEventGroupCreate();
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    nvs_flash_init();
  }

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
}

// Save credentials into NVS
void wifi_save_credentials(const char *ssid, const char *password) {
  if (!ssid || strlen(ssid) == 0)
    return;

  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err == ESP_OK) {
    nvs_set_str(handle, "ssid", ssid);
    nvs_set_str(handle, "pwd", password ? password : "");
    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "[NVS] Credentials saved successfully.");
  } else {
    ESP_LOGE(TAG, "[NVS] Failed to open NVS handle: %s", esp_err_to_name(err));
  }

  // Optional SD fallback (Ignored if read-only)
  FILE *f = fopen("/sdcard/system/manifest.json", "w");
  if (f) {
    cJSON *root = cJSON_CreateObject();
    cJSON *wifi = cJSON_CreateObject();
    cJSON_AddStringToObject(wifi, "ssid", ssid);
    cJSON_AddStringToObject(wifi, "password", password ? password : "");
    cJSON_AddItemToObject(root, "wifi", wifi);

    char *json_str = cJSON_Print(root);
    if (json_str) {
      fputs(json_str, f);
      cJSON_free(json_str);
    }
    fclose(f);
    cJSON_Delete(root);
  } else {
    ESP_LOGW(
        TAG,
        "[SD] SD Card read-only or unmounted; skipped writing manifest.json");
  }
}

// Load from SD Card manifest.json or NVS
void wifi_auto_connect_saved(void) {
  char ssid[33] = {0};
  char password[64] = {0};

  // Read SD Card Manifest JSON if present
  FILE *f = fopen("/sdcard/system/manifest.json", "r");
  if (f) {
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len > 0) {
      char *buf = malloc(len + 1);
      if (buf) {
        fread(buf, 1, len, f);
        buf[len] = '\0';

        cJSON *root = cJSON_Parse(buf);
        if (root) {
          cJSON *wifi = cJSON_GetObjectItem(root, "wifi");
          if (wifi) {
            cJSON *j_ssid = cJSON_GetObjectItem(wifi, "ssid");
            cJSON *j_pwd = cJSON_GetObjectItem(wifi, "password");

            if (cJSON_IsString(j_ssid) && strlen(j_ssid->valuestring) > 0) {
              strncpy(ssid, j_ssid->valuestring, sizeof(ssid) - 1);
              if (cJSON_IsString(j_pwd))
                strncpy(password, j_pwd->valuestring, sizeof(password) - 1);
              ESP_LOGI(TAG, "Loaded Wi-Fi credentials from SD manifest.json");
            }
          }
          cJSON_Delete(root);
        }
        free(buf);
      }
    }
    fclose(f);
  }

  // Fallback to NVS if SD manifest didn't supply an SSID
  if (strlen(ssid) == 0) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
      size_t ssid_len = sizeof(ssid);
      size_t pwd_len = sizeof(password);

      nvs_get_str(handle, "ssid", ssid, &ssid_len);
      nvs_get_str(handle, "pwd", password, &pwd_len);
      nvs_close(handle);

      if (strlen(ssid) > 0) {
        ESP_LOGI(TAG, "Loaded Wi-Fi credentials from NVS");
      }
    }
  }

  // Connect if valid SSID was loaded
  if (strlen(ssid) > 0) {
    ESP_LOGI(TAG, "Auto-connecting to: %s", ssid);
    wifi_manager_connect(ssid, password);
  }
}

uint16_t wifi_manager_scan(wifi_scan_entry_t **out_list) {
  wifi_scan_config_t scan_config = {
      .ssid = NULL, .bssid = NULL, .channel = 0, .show_hidden = true};

  ESP_LOGI(TAG, "Starting Wi-Fi Scan...");
  esp_wifi_scan_start(&scan_config, true);

  uint16_t ap_count = 0;
  esp_wifi_scan_get_ap_num(&ap_count);

  if (ap_count == 0)
    return 0;

  wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
  esp_wifi_scan_get_ap_records(&ap_count, ap_records);

  *out_list = malloc(sizeof(wifi_scan_entry_t) * ap_count);
  for (int i = 0; i < ap_count; i++) {
    strncpy((*out_list)[i].ssid, (char *)ap_records[i].ssid, 32);
    (*out_list)[i].ssid[32] = '\0';
    (*out_list)[i].rssi = ap_records[i].rssi;
    (*out_list)[i].authmode = ap_records[i].authmode;
  }

  free(ap_records);
  return ap_count;
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password) {
  wifi_config_t wifi_config = {0};
  strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
  if (password) {
    strncpy((char *)wifi_config.sta.password, password,
            sizeof(wifi_config.sta.password));
  }

  esp_wifi_disconnect();
  esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  return esp_wifi_connect();
}

bool wifi_manager_is_connected(void) {
  if (!s_wifi_event_group)
    return false;
  EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
  return (bits & WIFI_CONNECTED_BIT) != 0;
}