#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

esp_err_t dual_storage_init(void);
esp_err_t storage_enable_usb_msc(void);
esp_err_t storage_psram_flush_write(const char *path, const void *data,
                                    size_t len);
esp_err_t storage_remove_recursive(const char *path);