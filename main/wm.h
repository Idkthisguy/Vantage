#pragma once

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

#include "data/vantage_abi.h"

#include "software/process_manager.h"

typedef void (*wm_lifecycle_cb_t)(lv_obj_t *parent_container);

typedef struct {
  const char *id;
  const char *title;
  bool hide_from_recents;
  wm_lifecycle_cb_t on_create;
  wm_lifecycle_cb_t on_destroy;
  void *payload;
} app_descriptor_t;

typedef struct window_node {
  const app_descriptor_t *app;
  lv_obj_t *root_view;
  bool is_minimized;
  app_control_block_t *process_handle;
  struct window_node *prev;
} window_node_t;

typedef void (*wm_action_cb_t)(lv_obj_t *obj, void *user_data);

// --- System OS Lifecycle API ---
void wm_init(void);
void wm_open_app(const app_descriptor_t *app, app_control_block_t *process);
void wm_close_current(void);
void wm_minimize_current(void);
void wm_show_home(void);
void wm_show_app_switcher(void);

void wm_check_update_ui_state(void);
void wm_update_wifi_status(bool connected);

// System Keyboard
void wm_keyboard_attach(lv_obj_t *ta);
void wm_keyboard_hide(void);

// High-Level Window Manager API
lv_obj_t *wm_create_window(lv_obj_t *parent, const char *title);
lv_obj_t *wm_create_scroll_list(lv_obj_t *parent, int32_t w, int32_t h);

// Controls & Widgets
lv_obj_t *wm_add_text(lv_obj_t *parent, const char *text);
lv_obj_t *wm_add_button(lv_obj_t *parent, const char *label_text, int32_t w,
                        int32_t h);
lv_obj_t *wm_add_input(lv_obj_t *parent, const char *placeholder,
                       bool is_password);

// Event Handlers
void wm_on_tap(lv_obj_t *obj, wm_action_cb_t cb, void *user_data);
void wm_on_hold(lv_obj_t *obj, wm_action_cb_t cb, void *user_data);
void wm_on_scroll(lv_obj_t *obj, wm_action_cb_t cb, void *user_data);

// OS Utilities
void wm_toast(const char *message, uint32_t duration_ms);

void wm_register_sdk_app(const vantage_app_desc_t *sdk_app, void *payload);

void wm_reload_home(void);

void wm_lvgl_lock(void);
void wm_lvgl_unlock(void);