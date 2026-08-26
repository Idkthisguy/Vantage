#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

bool ota_is_update_available(void);
void ota_get_current_version(char *out_version, size_t max_len);
void ota_check_and_download_task(void *pvParameters);
void execute_sd_ota_flash(void);

#ifdef __cplusplus
}
#endif