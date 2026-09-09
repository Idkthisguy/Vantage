#pragma once

#include "data/vantage_abi.h"
#include "wm.h"
#include <esp_elf.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* I've been using Opera GX on Linux because it helps me heal
from whatever this fucking crap is oh my fucking god i wanna kms */

// Map SDK descriptor directly to ABI descriptor type
typedef vantage_app_desc_t sdk_app_desc_t;

/* Public functions used by portable apps (declared in app_sdk/vantage_app.h). */
void *sdk_create_window(const char *title);
void *sdk_add_button(void *parent, const char *label,
                     void (*on_click_cb)(void *user_data), void *user_data);
void *sdk_add_text(void *parent, const char *text);
void sdk_show_toast(const char *message, uint32_t duration_ms);
void sdk_get_data_path(const char *filename, char *out_path, size_t max_len);

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
