#pragma once

#include "data/vantage_abi.h"
#include "wm.h"
#include <esp_elf.h>

#ifdef __cplusplus
extern "C" {
#endif

// Map SDK descriptor directly to ABI descriptor type
typedef vantage_app_desc_t sdk_app_desc_t;

typedef struct {
  const sdk_app_desc_t *sdk_app;
  void *payload;
  esp_elf_t elf_handle; // Holds loaded segment/text pointers
} sdk_app_instance_t;

void sdk_bridge_set_active_app(const sdk_app_desc_t *app_desc,
                               const char *mount_point, void *payload);
app_descriptor_t sdk_bridge_register_app(const sdk_app_desc_t *sdk_app,
                                         const char *mount_point);

app_descriptor_t *sdk_bridge_create_os_app(const sdk_app_desc_t *sdk_app,
                                           const char *mount_point,
                                           void *payload);

#ifdef __cplusplus
}
#endif