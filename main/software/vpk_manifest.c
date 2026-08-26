#include "vpk_manifest.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

bool manifest_parse_json(const char *json_string,
                         vpk_manifest_t *out_manifest) {
  if (!json_string || !out_manifest)
    return false;

  cJSON *root = cJSON_Parse(json_string);
  if (!root)
    return false;

  cJSON *id = cJSON_GetObjectItem(root, "id");
  cJSON *name = cJSON_GetObjectItem(root, "name");
  cJSON *ver = cJSON_GetObjectItem(root, "version");
  cJSON *exec = cJSON_GetObjectItem(root, "exec");
  cJSON *icon = cJSON_GetObjectItem(root, "icon");

  bool valid = (cJSON_IsString(id) && cJSON_IsString(name));

  if (valid) {
    strncpy(out_manifest->id, id->valuestring, sizeof(out_manifest->id) - 1);
    strncpy(out_manifest->name, name->valuestring,
            sizeof(out_manifest->name) - 1);
    strncpy(out_manifest->version,
            cJSON_IsString(ver) ? ver->valuestring : "1.0.0",
            sizeof(out_manifest->version) - 1);
    strncpy(out_manifest->exec,
            cJSON_IsString(exec) ? exec->valuestring : "main.lua",
            sizeof(out_manifest->exec) - 1);
    strncpy(out_manifest->icon,
            cJSON_IsString(icon) ? icon->valuestring : "icon.bin",
            sizeof(out_manifest->icon) - 1);
  }

  cJSON_Delete(root);
  return valid;
}

void manifest_get_app_dir(const char *base_path, const char *app_id,
                          char *out_path, size_t max_len) {
  snprintf(out_path, max_len, "%s/apps/%s", base_path ? base_path : "/sdcard",
           app_id);
}

void manifest_get_data_dir(const char *base_path, const char *app_id,
                           char *out_path, size_t max_len) {
  snprintf(out_path, max_len, "%s/apps/%s/data",
           base_path ? base_path : "/sdcard", app_id);
}