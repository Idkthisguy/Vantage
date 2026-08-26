#pragma once

#include "esp_err.h"
#include "software/vpk_manifest.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Prepares isolated sandbox paths under the specified storage mount
esp_err_t app_sandbox_create_directories(const char *base_path,
                                         const char *app_id);

// Appends installed VPK metadata into <base_path>/apps/installed_apps.json
esp_err_t app_registry_add(const char *base_path,
                           const vpk_manifest_t *manifest);

// Removes an app entry and recursively wipes its directory
esp_err_t app_registry_remove(const char *base_path, const char *app_id);

// Checks if app is installed on specified storage mount
bool app_registry_is_installed(const char *base_path, const char *app_id);

#ifdef __cplusplus
}
#endif