#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t fs_init(void);
void fs_create_default_dirs(void);

#ifdef __cplusplus
}
#endif