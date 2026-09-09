#pragma once

/*
 * Public, portable Vantage app API.
 *
 * App code includes only this header. The same source can be compiled for an
 * ESP VPK and for the SDL desktop simulator. UI values are opaque on purpose:
 * apps do not need LVGL or ESP-IDF headers to create a basic Vantage UI.
 */

#include "data/vantage_abi.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*vantage_click_cb_t)(void *user_data);

void *sdk_create_window(const char *title);
void *sdk_add_text(void *parent, const char *text);
void *sdk_add_button(void *parent, const char *label,
                     vantage_click_cb_t on_click_cb, void *user_data);
void sdk_show_toast(const char *message, uint32_t duration_ms);
void sdk_get_data_path(const char *filename, char *out_path, size_t max_len);

/* Every portable app exports one uniquely named descriptor function. */
typedef const vantage_app_desc_t *(*vantage_app_descriptor_fn_t)(void);

#ifdef __cplusplus
}
#endif
