#include "drivers/ota_engine.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "OTA_ENGINE";

#define NVS_SYS_NAMESPACE "sys_config"
#define DEFAULT_VERSION "v1.0.0"
#define UPDATE_FILE_PATH "/sdcard/update/firmware.bin"

static bool update_ready = false;
static char pending_version_tag[32] = {0};

typedef struct {
  char *buffer;
  int length;
  int max_len;
} api_response_buffer_t;

static esp_err_t api_http_event_handler(esp_http_client_event_t *evt) {
  if (evt->event_id == HTTP_EVENT_ON_DATA) {
    api_response_buffer_t *resp = (api_response_buffer_t *)evt->user_data;
    if (resp && resp->buffer) {
      int copy_len = evt->data_len;
      if (resp->length + copy_len >= resp->max_len - 1) {
        copy_len = resp->max_len - 1 - resp->length;
      }
      if (copy_len > 0) {
        memcpy(resp->buffer + resp->length, evt->data, copy_len);
        resp->length += copy_len;
        resp->buffer[resp->length] = '\0';
      }
    }
  }
  return ESP_OK;
}

static esp_err_t download_http_event_handler(esp_http_client_event_t *evt) {
  switch (evt->event_id) {
  case HTTP_EVENT_REDIRECT:
    // Strip GitHub headers on redirect so AWS S3 accepts the request
    esp_http_client_delete_header(evt->client, "Authorization");
    esp_http_client_delete_header(evt->client, "Accept");
    break;
  case HTTP_EVENT_ON_DATA:
    if (!esp_http_client_is_chunked_response(evt->client)) {
      if (evt->user_data) {
        FILE *fp = (FILE *)evt->user_data;
        fwrite(evt->data, 1, evt->data_len, fp);
      }
    }
    break;
  default:
    break;
  }
  return ESP_OK;
}

bool ota_is_update_available(void) { return update_ready; }

void ota_get_current_version(char *out_version, size_t max_len) {
  bool version_found = false;

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
          cJSON *sys = cJSON_GetObjectItem(root, "system");
          if (sys) {
            cJSON *ver = cJSON_GetObjectItem(sys, "version");
            if (cJSON_IsString(ver) && (ver->valuestring != NULL)) {
              strncpy(out_version, ver->valuestring, max_len - 1);
              out_version[max_len - 1] = '\0';
              version_found = true;
              ESP_LOGI(TAG, "Loaded version %s from manifest.json",
                       out_version);
            }
          }
          cJSON_Delete(root);
        }
        free(buf);
      }
    }
    fclose(f);
  }

  if (!version_found) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_SYS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
      size_t required_size = max_len;
      err = nvs_get_str(handle, "sys_ver", out_version, &required_size);
      if (err != ESP_OK) {
        strncpy(out_version, DEFAULT_VERSION, max_len - 1);
        nvs_set_str(handle, "sys_ver", DEFAULT_VERSION);
        nvs_commit(handle);
      }
      nvs_close(handle);
    } else {
      strncpy(out_version, DEFAULT_VERSION, max_len - 1);
    }
  }
}

static void ota_set_current_version(const char *new_version) {
  // 1. Save to NVS
  nvs_handle_t handle;
  if (nvs_open(NVS_SYS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
    nvs_set_str(handle, "sys_ver", new_version);
    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "Updated NVS sys_ver to %s", new_version);
  }

  FILE *f = fopen("/sdcard/system/manifest.json", "r+");
  if (!f)
    f = fopen("/sdcard/system/manifest.json", "w");

  if (f) {
    cJSON *root = NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len > 0) {
      char *buf = malloc(len + 1);
      if (buf) {
        fread(buf, 1, len, f);
        buf[len] = '\0';
        root = cJSON_Parse(buf);
        free(buf);
      }
    }

    if (!root)
      root = cJSON_CreateObject();

    cJSON *sys = cJSON_GetObjectItem(root, "system");
    if (!sys) {
      sys = cJSON_CreateObject();
      cJSON_AddItemToObject(root, "system", sys);
    }

    cJSON_ReplaceItemInObject(sys, "version", cJSON_CreateString(new_version));

    char *json_out = cJSON_Print(root);
    if (json_out) {
      fclose(f);
      f = fopen("/sdcard/system/manifest.json", "w");
      if (f) {
        fputs(json_out, f);
        fclose(f);
      }
      cJSON_free(json_out);
    } else {
      fclose(f);
    }
    cJSON_Delete(root);
  }
}

static void parse_version(const char *ver_str, int *major, int *minor,
                          int *patch) {
  *major = 0;
  *minor = 0;
  *patch = 0;
  if (ver_str[0] == 'v' || ver_str[0] == 'V')
    ver_str++;
  sscanf(ver_str, "%d.%d.%d", major, minor, patch);
}

static bool is_remote_newer(const char *remote_ver, const char *local_ver) {
  int r_maj, r_min, r_pat;
  int l_maj, l_min, l_pat;

  parse_version(remote_ver, &r_maj, &r_min, &r_pat);
  parse_version(local_ver, &l_maj, &l_min, &l_pat);

  if (r_maj != l_maj)
    return r_maj > l_maj;
  if (r_min != l_min)
    return r_min > l_min;
  return r_pat > l_pat;
}

void ota_check_and_download_task(void *pvParameters) {
  ESP_LOGI(TAG, "Checking GitHub Releases...");

  char local_ver[32] = {0};
  ota_get_current_version(local_ver, sizeof(local_ver));

  char auth_header[128];

  int max_json_size = 32768; // Allocate 32KB buffer for API response
  api_response_buffer_t json_resp = {
      .buffer = malloc(max_json_size), .length = 0, .max_len = max_json_size};

  if (!json_resp.buffer) {
    ESP_LOGE(TAG, "Failed to allocate memory for JSON buffer!");
    vTaskDelete(NULL);
    return;
  }
  json_resp.buffer[0] = '\0';

  esp_http_client_config_t config = {
      .url = "YOUR LINK HERE", // Change this to the link your repo
      .timeout_ms = 15000,
      .buffer_size = 4096,
      .buffer_size_tx = 2048,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .event_handler = api_http_event_handler,
      .user_data = &json_resp,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_http_client_set_header(client, "User-Agent", "ESP32S3-VantageOS");
  esp_http_client_set_header(client, "Authorization", auth_header);
  esp_http_client_set_header(client, "Accept", "application/vnd.github+json");
  esp_http_client_set_header(client, "X-GitHub-Api-Version", "2022-11-28");

  esp_err_t err = esp_http_client_perform(client);
  int status_code = esp_http_client_get_status_code(client);

  esp_http_client_cleanup(client);

  if (err != ESP_OK || status_code != 200) {
    ESP_LOGE(TAG, "GitHub API HTTP Request Failed! Code: %d, Err: %s",
             status_code, esp_err_to_name(err));
    free(json_resp.buffer);
    vTaskDelete(NULL);
    return;
  }

  ESP_LOGI(TAG, "Successfully fetched FULL API Payload: %d bytes",
           json_resp.length);

  cJSON *root = cJSON_Parse(json_resp.buffer);
  free(json_resp.buffer);

  if (!root) {
    ESP_LOGE(TAG, "Failed to parse release JSON!");
    vTaskDelete(NULL);
    return;
  }

  cJSON *tag_name = cJSON_GetObjectItem(root, "tag_name");
  if (tag_name && cJSON_IsString(tag_name)) {
    ESP_LOGI(TAG, "Latest Release Tag: %s (Local: %s)", tag_name->valuestring,
             local_ver);

    if (is_remote_newer(tag_name->valuestring, local_ver)) {
      cJSON *assets = cJSON_GetObjectItem(root, "assets");
      cJSON *asset = NULL;
      const char *download_url = NULL;

      if (cJSON_IsArray(assets)) {
        cJSON_ArrayForEach(asset, assets) {
          cJSON *name = cJSON_GetObjectItem(asset, "name");
          if (name && strstr(name->valuestring, "firmware.bin")) {
            cJSON *url_item = cJSON_GetObjectItem(asset, "url");
            if (url_item)
              download_url = url_item->valuestring;
            break;
          }
        }
      }

      // Change asset download logic inside ota_check_and_download_task:
      if (download_url) {
        ESP_LOGI(TAG, "Downloading OS asset Endpoint: %s", download_url);

        FILE *fp = fopen(UPDATE_FILE_PATH, "wb");
        if (!fp) {
          ESP_LOGE(TAG, "Failed to open SD card file for writing!");
          cJSON_Delete(root);
          vTaskDelete(NULL);
          return;
        }

        esp_http_client_config_t dl_config = {
            .url = download_url,
            .timeout_ms = 30000,
            .buffer_size = 4096,
            .buffer_size_tx = 2048,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .disable_auto_redirect = false,
            .max_redirection_count = 5,
            .event_handler = download_http_event_handler,
            .user_data = fp,
        };

        esp_http_client_handle_t dl_client = esp_http_client_init(&dl_config);

        // CRITICAL: GitHub API asset endpoint requires User-Agent & Accept
        esp_http_client_set_header(dl_client, "User-Agent",
                                   "ESP32S3-VantageOS");
        esp_http_client_set_header(dl_client, "Authorization", auth_header);
        esp_http_client_set_header(dl_client, "Accept",
                                   "application/octet-stream");

        esp_err_t dl_err = esp_http_client_perform(dl_client);
        int dl_status = esp_http_client_get_status_code(dl_client);

        fclose(fp);

        // Valid binary check: must be HTTP 200 AND larger than 100KB (0x10000
        // header sanity)
        struct stat st;
        if (dl_err == ESP_OK && dl_status == 200 &&
            stat(UPDATE_FILE_PATH, &st) == 0 && st.st_size > 100000) {
          strncpy(pending_version_tag, tag_name->valuestring,
                  sizeof(pending_version_tag) - 1);
          update_ready = true;
          ESP_LOGI(TAG, "Successfully downloaded binary to SD card: %ld bytes!",
                   (long)st.st_size);
        } else {
          ESP_LOGE(TAG, "Download failed or invalid binary! HTTP Code: %d",
                   dl_status);
          remove(UPDATE_FILE_PATH); // Only clean up failed/corrupted partial
                                    // downloads
        }

        esp_http_client_cleanup(dl_client);
      } else {
        ESP_LOGE(TAG, "Could not find 'firmware.bin' asset in release.");
      }
    } else {
      ESP_LOGI(TAG, "System is already up to date.");
    }
  }

  cJSON_Delete(root);
  vTaskDelete(NULL);
}

static void ota_flash_task(void *pvParameters) {
  FILE *f = fopen(UPDATE_FILE_PATH, "rb");
  if (!f) {
    ESP_LOGE(TAG, "No update file found on SD Card!");
    vTaskDelete(NULL);
    return;
  }

  const esp_partition_t *update_partition =
      esp_ota_get_next_update_partition(NULL);
  if (!update_partition) {
    ESP_LOGE(TAG, "No valid OTA partition found!");
    fclose(f);
    vTaskDelete(NULL);
    return;
  }

  esp_ota_handle_t update_handle = 0;
  esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES,
                                &update_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init OTA flash: %s", esp_err_to_name(err));
    fclose(f);
    vTaskDelete(NULL);
    return;
  }

  char buffer[4096];
  size_t bytes_read = 0;
  bool write_failed = false;

  while ((bytes_read = fread(buffer, 1, sizeof(buffer), f)) > 0) {
    err = esp_ota_write(update_handle, buffer, bytes_read);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Flash write failed: %s! Aborting update.",
               esp_err_to_name(err));
      write_failed = true;
      break;
    }
    // Yield execution to allow other tasks (like Watchdog and UI) to breathe
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  fclose(f);

  if (write_failed) {
    esp_ota_abort(update_handle);
    remove(UPDATE_FILE_PATH);
    vTaskDelete(NULL);
    return;
  }

  if (esp_ota_end(update_handle) == ESP_OK) {
    err = esp_ota_set_boot_partition(update_partition);
    if (err == ESP_OK) {
      if (strlen(pending_version_tag) > 0) {
        ota_set_current_version(pending_version_tag);
      }
      ESP_LOGI(TAG, "Flashing Complete! Rebooting System...");
      vTaskDelay(pdMS_TO_TICKS(1000));
      esp_restart();
    } else {
      ESP_LOGE(TAG, "Failed to set boot partition: %s", esp_err_to_name(err));
    }
  } else {
    ESP_LOGE(TAG, "esp_ota_end failed!");
  }

  vTaskDelete(NULL);
}

void execute_sd_ota_flash(void) {
  // Spawn a background task on Core 0 so it doesn't freeze Core 1's LVGL UI
  // thread
  xTaskCreatePinnedToCore(ota_flash_task, "ota_flash_task", 8192, NULL, 5, NULL,
                          0);
}