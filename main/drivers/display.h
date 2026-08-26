#pragma once

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

void display_init(void);
void display_lvgl_init(void);

void display_draw_raw_panic(const char *title, const char *details,
                            const uint8_t *icon_16x16, uint16_t bg_color,
                            uint16_t fg_color, const uint8_t font[128][8]);

#ifdef __cplusplus
}
#endif