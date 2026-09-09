#pragma once

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Desktop implementation of the public window-manager surface used on device. */
typedef void (*wm_lifecycle_cb_t)(lv_obj_t *parent_container);
typedef void (*wm_action_cb_t)(lv_obj_t *obj, void *user_data);

typedef struct {
  const char *id;
  const char *title;
  bool hide_from_recents;
  wm_lifecycle_cb_t on_create;
  wm_lifecycle_cb_t on_destroy;
  void *payload;
} app_descriptor_t;

void wm_init(void);
void wm_open_app(const app_descriptor_t *app, void *process);
void wm_open_app_animated(lv_obj_t *src, const app_descriptor_t *app,
                          void *process);
void wm_close_current(void);
void wm_minimize_current(void);
void wm_show_home(void);
void wm_show_app_switcher(void);

lv_obj_t *wm_create_window(lv_obj_t *parent, const char *title);
lv_obj_t *wm_create_scroll_list(lv_obj_t *parent, int32_t w, int32_t h);
lv_obj_t *wm_add_text(lv_obj_t *parent, const char *text);
lv_obj_t *wm_add_button(lv_obj_t *parent, const char *label, int32_t w,
                        int32_t h);
lv_obj_t *wm_add_input(lv_obj_t *parent, const char *placeholder,
                       bool is_password);

void wm_on_tap(lv_obj_t *obj, wm_action_cb_t cb, void *user_data);
void wm_on_hold(lv_obj_t *obj, wm_action_cb_t cb, void *user_data);
void wm_on_scroll(lv_obj_t *obj, wm_action_cb_t cb, void *user_data);
void wm_toast(const char *message, uint32_t duration_ms);
void wm_keyboard_attach(lv_obj_t *ta);
void wm_keyboard_hide(void);
void wm_update_wifi_status(bool connected);
void wm_reload_home(void);

/* Files in the desktop model live under this folder, never at device mounts. */
const char *wm_sim_data_root(void);

#ifdef __cplusplus
}
#endif
