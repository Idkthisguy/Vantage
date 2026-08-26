#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif

// Loads file from SD directly into Octal PSRAM
void *asset_load_to_psram(const char *file_path, size_t *out_size);

// Frees allocated PSRAM memory
void asset_free_psram(void *buffer);

#ifdef __cplusplus
}
#endif