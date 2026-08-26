#include "package/app_registry.h"
#include "cJSON.h"
#include "drivers/storage.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "APP_REGISTRY";

static void get_registry_path(const char *base_path, char *out_buf,
                              size_t max_len) {
  snprintf(out_buf, max_len, "%s/apps/installed_apps.json",
           base_path ? base_path : "/sdcard");
}

static char *read_file_to_psram(const char *path, size_t *out_size) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;

  fseek(f, 0, SEEK_END);
  size_t size = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *buf = (char *)heap_caps_malloc(size + 1, MALLOC_CAP_SPIRAM);
  if (buf) {
    fread(buf, 1, size, f);
    buf[size] = '\0';
    if (out_size)
      *out_size = size;
  }
  fclose(f);
  return buf;
}

esp_err_t app_sandbox_create_directories(const char *base_path,
                                         const char *app_id) {
  char path_buf[256];
  const char *root = base_path ? base_path : "/sdcard";

  snprintf(path_buf, sizeof(path_buf), "%s/apps", root);
  if (mkdir(path_buf, 0775) != 0 && errno != EEXIST) {
    ESP_LOGE(TAG, "Failed creating base apps dir: %s (errno: %d)", path_buf,
             errno);
    return ESP_FAIL;
  }

  snprintf(path_buf, sizeof(path_buf), "%s/apps/%s", root, app_id);
  if (mkdir(path_buf, 0775) != 0 && errno != EEXIST) {
    ESP_LOGE(TAG, "Failed creating app dir: %s (errno: %d)", path_buf, errno);
    return ESP_FAIL;
  }

  snprintf(path_buf, sizeof(path_buf), "%s/apps/%s/data", root, app_id);
  if (mkdir(path_buf, 0775) != 0 && errno != EEXIST) {
    ESP_LOGE(TAG, "Failed creating data dir: %s (errno: %d)", path_buf, errno);
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t app_registry_add(const char *base_path,
                           const vpk_manifest_t *manifest) {
  cJSON *root = NULL;
  size_t file_size = 0;
  char reg_path[256];
  char apps_dir[256];

  const char *root_mount = base_path ? base_path : "/sdcard";
  snprintf(apps_dir, sizeof(apps_dir), "%s/apps", root_mount);

  // Ensure parent /apps directory exists before writing registry file
  struct stat st;
  if (stat(apps_dir, &st) != 0) {
    mkdir(apps_dir, 0775);
  }

  get_registry_path(base_path, reg_path, sizeof(reg_path));
  char *json_raw = read_file_to_psram(reg_path, &file_size);

  if (json_raw) {
    root = cJSON_ParseWithLength(json_raw, file_size);
    heap_caps_free(json_raw);
  }

  if (!root || !cJSON_IsArray(root)) {
    if (root)
      cJSON_Delete(root);
    root = cJSON_CreateArray();
  }

  int array_size = cJSON_GetArraySize(root);
  for (int i = 0; i < array_size; i++) {
    cJSON *item = cJSON_GetArrayItem(root, i);
    cJSON *id_item = cJSON_GetObjectItem(item, "id");
    if (cJSON_IsString(id_item) &&
        strcmp(id_item->valuestring, manifest->id) == 0) {
      cJSON_DeleteItemFromArray(root, i);
      break;
    }
  }

  cJSON *app_obj = cJSON_CreateObject();
  cJSON_AddStringToObject(app_obj, "id", manifest->id);
  cJSON_AddStringToObject(app_obj, "name", manifest->name);
  cJSON_AddStringToObject(app_obj, "version", manifest->version);
  cJSON_AddStringToObject(app_obj, "exec", manifest->exec);
  cJSON_AddStringToObject(app_obj, "icon", manifest->icon);

  cJSON_AddItemToArray(root, app_obj);

  char *rendered_json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);

  if (!rendered_json)
    return ESP_ERR_NO_MEM;

  esp_err_t write_err =
      storage_psram_flush_write(reg_path, rendered_json, strlen(rendered_json));
  free(rendered_json);

  if (write_err == ESP_OK) {
    ESP_LOGI(TAG, "Registered app '%s' successfully at %s!", manifest->id,
             reg_path);
  }
  return write_err;
}

esp_err_t app_registry_remove(const char *base_path, const char *app_id) {
  if (!app_id)
    return ESP_ERR_INVALID_ARG;

  const char *root_mount = base_path ? base_path : "/sdcard";

  char app_dir[256];
  manifest_get_app_dir(root_mount, app_id, app_dir, sizeof(app_dir));
  storage_remove_recursive(app_dir);
  char reg_path[256];
  get_registry_path(root_mount, reg_path, sizeof(reg_path));

  size_t file_size = 0;
  char *json_raw = read_file_to_psram(reg_path, &file_size);
  if (!json_raw)
    return ESP_OK;

  cJSON *root = cJSON_ParseWithLength(json_raw, file_size);
  heap_caps_free(json_raw);

  if (root && cJSON_IsArray(root)) {
    int array_size = cJSON_GetArraySize(root);
    bool modified = false;

    for (int i = 0; i < array_size; i++) {
      cJSON *item = cJSON_GetArrayItem(root, i);
      cJSON *id_item = cJSON_GetObjectItem(item, "id");
      if (cJSON_IsString(id_item) &&
          strcmp(id_item->valuestring, app_id) == 0) {
        cJSON_DeleteItemFromArray(root, i);
        modified = true;
        break;
      }
    }

    if (modified) {
      char *rendered = cJSON_PrintUnformatted(root);
      if (rendered) {
        storage_psram_flush_write(reg_path, rendered, strlen(rendered));
        free(rendered);
      }
    }
    cJSON_Delete(root);
  }

  return ESP_OK;
}

bool app_registry_is_installed(const char *base_path, const char *app_id) {
  char app_dir[256];
  manifest_get_app_dir(base_path, app_id, app_dir, sizeof(app_dir));
  struct stat st;
  return (stat(app_dir, &st) == 0);
}