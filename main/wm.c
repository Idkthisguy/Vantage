#include "wm.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

#include "drivers/ota_engine.h"
#include "drivers/wifi.h"

#include "apps/file_manager.h"

#include "cJSON.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "package/app_registry.h"
#include "process_manager.h"
#include "software/sdk_bridge.h"

#include <esp_elf.h>
#include <sys/stat.h>

#include "esp_attr.h"

#include "esp_system.h"

LV_FONT_DECLARE(lv_font_montserrat_10);

typedef struct {
  uint32_t magic;
  char panic_msg[1024];
} rtc_crash_store_t;

#define CRASH_MAGIC 0xDEADBEEF

RTC_NOINIT_ATTR static rtc_crash_store_t rtc_crash_data;

#include "esp_debug_helpers.h"

void __attribute__((used)) esp_panic_on_dump_end(void) {
  rtc_crash_data.magic = CRASH_MAGIC;

  char *buf = rtc_crash_data.panic_msg;
  size_t max_len = sizeof(rtc_crash_data.panic_msg);

  esp_reset_reason_t reason = esp_reset_reason();
  size_t offset = snprintf(
      buf, max_len, "CRASH REASON ENUM: %d\n\nBacktrace:\n", (int)reason);

  esp_backtrace_frame_t stk_frame;
  esp_backtrace_get_start(&(stk_frame.pc), &(stk_frame.sp),
                          &(stk_frame.next_pc));

  for (int i = 0; i < 10 && stk_frame.pc != 0; i++) {
    offset += snprintf(buf + offset, max_len - offset, "0x%08X ",
                       (unsigned int)stk_frame.pc);
    if (!esp_backtrace_get_next_frame(&stk_frame))
      break;
  }
}

#include "apps/app_launcher.h"

static const char *TAG = "WINDOW_MANAGER";

#define MAX_APP_NAME_CHARS 8

static lv_obj_t *main_screen = NULL;
static lv_obj_t *status_bar = NULL;
static lv_obj_t *nav_bar = NULL;
static lv_obj_t *app_container = NULL;
static lv_obj_t *sys_keyboard = NULL;

static window_node_t *window_stack = NULL;

extern const app_descriptor_t wifi_app;

static void format_app_title(const char *src, char *dst, size_t dst_size,
                             size_t max_chars) {
  if (strlen(src) <= max_chars) {
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
  } else {
    snprintf(dst, dst_size, "%.*s...", (int)max_chars, src);
  }
}

static lv_obj_t *add_grid_app_item(lv_obj_t *parent, const char *title,
                                   const char *symbol, size_t max_chars) {
  // Cell container holding both button and text
  lv_obj_t *cell = lv_obj_create(parent);
  lv_obj_remove_style_all(cell);
  lv_obj_set_size(cell, 68, 75);
  lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_gap(cell, 4, 0);

  // App Button
  lv_obj_t *btn = lv_btn_create(cell);
  lv_obj_set_size(btn, 52, 52);
  lv_obj_set_style_radius(btn, 12, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x00E5FF), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x0088A3), LV_STATE_PRESSED);

  lv_obj_t *icon_lbl = lv_label_create(btn);
  lv_label_set_text(icon_lbl, symbol ? symbol : LV_SYMBOL_FILE);
  lv_obj_center(icon_lbl);

  // Truncated Label Below Button
  char display_name[32];
  format_app_title(title, display_name, sizeof(display_name), max_chars);

  lv_obj_t *name_lbl = lv_label_create(cell);
  lv_label_set_text(name_lbl, display_name);
  lv_obj_set_style_text_color(name_lbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_10, 0);

  return btn;
}

typedef struct {
  wm_action_cb_t tap_cb;
  wm_action_cb_t hold_cb;
  wm_action_cb_t scroll_cb;
  void *user_data;
  bool hold_triggered;
} wm_event_ctx_t;

static void global_event_dispatcher(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *target = lv_event_get_target(e);
  wm_event_ctx_t *ctx = (wm_event_ctx_t *)lv_event_get_user_data(e);

  if (!ctx)
    return;

  if (code == LV_EVENT_PRESSED) {
    ctx->hold_triggered = false;
  } else if (code == LV_EVENT_LONG_PRESSED) {
    ctx->hold_triggered = true;
    if (ctx->hold_cb) {
      ctx->hold_cb(target, ctx->user_data);
    }
  } else if (code == LV_EVENT_CLICKED) {
    if (ctx->hold_triggered) {
      ctx->hold_triggered = false;
      return;
    }
    if (ctx->tap_cb) {
      ctx->tap_cb(target, ctx->user_data);
    }
  } else if (code == LV_EVENT_SCROLL && ctx->scroll_cb) {
    ctx->scroll_cb(target, ctx->user_data);
  }
}

static void host_lv_obj_set_style_bg_color_hex(lv_obj_t *obj,
                                               uint32_t hex_color,
                                               lv_style_selector_t selector) {
  lv_obj_set_style_bg_color(obj, lv_color_hex(hex_color), selector);
}

static void host_lv_obj_set_style_text_color_hex(lv_obj_t *obj,
                                                 uint32_t hex_color,
                                                 lv_style_selector_t selector) {
  lv_obj_set_style_text_color(obj, lv_color_hex(hex_color), selector);
}

extern void *sdk_add_button(void *parent, const char *label,
                            void (*on_click_cb)(void *user_data),
                            void *user_data);
extern void *sdk_add_text(void *parent, const char *text);
extern void sdk_show_toast(const char *message, uint32_t duration_ms);
extern void sdk_get_data_path(const char *filename, char *out_path,
                              size_t max_len);

static const esp_elf_symbol_table_t host_symbols[] = {
    {"sdk_bridge_set_active_app", (void *)sdk_bridge_set_active_app},
    {"sdk_bridge_create_os_app", (void *)sdk_bridge_create_os_app},
    {"sdk_bridge_register_app", (void *)sdk_bridge_register_app},

    // C Standard Library Symbols
    {"snprintf", (void *)snprintf},
    {"calloc", (void *)calloc},
    {"malloc", (void *)malloc},
    {"free", (void *)free},
    {"memcpy", (void *)memcpy},
    {"memset", (void *)memset},
    {"printf", (void *)printf},

    // System APIs
    {"sdk_show_toast", (void *)sdk_show_toast},
    {"sdk_get_data_path", (void *)sdk_get_data_path},

    // LVGL Core Symbol Exports
    {"lv_label_create", (void *)lv_label_create},
    {"lv_label_set_text", (void *)lv_label_set_text},
    {"lv_btn_create", (void *)lv_btn_create},
    {"lv_obj_set_size", (void *)lv_obj_set_size},
    {"lv_obj_align", (void *)lv_obj_align},
    {"lv_obj_center", (void *)lv_obj_center},
    {"lv_obj_add_event_cb", (void *)lv_obj_add_event_cb},
    {"lv_obj_set_user_data", (void *)lv_obj_set_user_data},
    {"lv_obj_get_user_data", (void *)lv_obj_get_user_data},
    {"lv_event_get_code", (void *)lv_event_get_code},
    {"lv_obj_remove_style_all", (void *)lv_obj_remove_style_all},
    {"lv_obj_set_flex_flow", (void *)lv_obj_set_flex_flow},
    {"lv_obj_set_flex_align", (void *)lv_obj_set_flex_align},
    {"lv_obj_set_style_pad_gap", (void *)lv_obj_set_style_pad_gap},
    {"lv_obj_set_style_bg_color", (void *)lv_obj_set_style_bg_color},
    {"lv_obj_set_style_bg_opa", (void *)lv_obj_set_style_bg_opa},
    {"lv_obj_set_style_text_color", (void *)lv_obj_set_style_text_color},
    {"lv_color_hex", (void *)lv_color_hex},

    {NULL, NULL}};

static void kb_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *ta = lv_keyboard_get_textarea(sys_keyboard);

  if (!ta)
    return;

  if (code == LV_EVENT_READY) {
    lv_obj_remove_state(ta, LV_STATE_FOCUSED);
    lv_obj_send_event(ta, LV_EVENT_READY, NULL);
    wm_keyboard_hide();
  } else if (code == LV_EVENT_CANCEL) {
    lv_obj_remove_state(ta, LV_STATE_FOCUSED);
    wm_keyboard_hide();
  }
}

static void create_system_keyboard(void) {
  sys_keyboard = lv_keyboard_create(lv_layer_top());
  lv_obj_set_size(sys_keyboard, 320, 180);
  lv_obj_align(sys_keyboard, LV_ALIGN_BOTTOM_MID, 0, 180);

  lv_obj_clear_flag(sys_keyboard, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_clear_flag(sys_keyboard, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_add_event_cb(sys_keyboard, kb_event_cb, LV_EVENT_ALL, NULL);
}

void wm_keyboard_attach(lv_obj_t *ta) {
  if (!sys_keyboard)
    return;
  lv_obj_move_foreground(sys_keyboard);
  lv_keyboard_set_textarea(sys_keyboard, ta);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, sys_keyboard);
  lv_anim_set_values(&a, 180, 0);
  lv_anim_set_duration(&a, 150);
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
  lv_anim_start(&a);
}

void wm_keyboard_hide(void) {
  if (!sys_keyboard)
    return;

  lv_keyboard_set_textarea(sys_keyboard, NULL);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, sys_keyboard);
  lv_anim_set_values(&a, lv_obj_get_y(sys_keyboard), 180);
  lv_anim_set_duration(&a, 150);
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
  lv_anim_start(&a);
}

static void nav_back_cb(lv_event_t *e) { wm_close_current(); }

static void nav_home_cb(lv_event_t *e) { wm_minimize_current(); }

static void nav_recents_cb(lv_event_t *e) { wm_show_app_switcher(); }

static void create_navigation_bar(lv_obj_t *parent) {
  nav_bar = lv_obj_create(parent);
  lv_obj_remove_style_all(nav_bar);
  lv_obj_set_size(nav_bar, 320, 30);
  lv_obj_align(nav_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(nav_bar, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(nav_bar, LV_OPA_COVER, 0);

  // Flex layout for equal spacing
  lv_obj_set_flex_flow(nav_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(nav_bar, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Back Button
  lv_obj_t *btn_back = lv_btn_create(nav_bar);
  lv_obj_set_size(btn_back, 60, 26);
  lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, 0);
  lv_obj_t *lbl_back = lv_label_create(btn_back);
  lv_label_set_text(lbl_back, "Back");
  lv_obj_center(lbl_back);
  lv_obj_add_event_cb(btn_back, nav_back_cb, LV_EVENT_CLICKED, NULL);

  // Home Button
  lv_obj_t *btn_home = lv_btn_create(nav_bar);
  lv_obj_set_size(btn_home, 60, 26);
  lv_obj_set_style_bg_opa(btn_home, LV_OPA_TRANSP, 0);
  lv_obj_t *lbl_home = lv_label_create(btn_home);
  lv_label_set_text(lbl_home, "Home");
  lv_obj_center(lbl_home);
  lv_obj_add_event_cb(btn_home, nav_home_cb, LV_EVENT_CLICKED, NULL);

  // Recents Button
  lv_obj_t *btn_recents = lv_btn_create(nav_bar);
  lv_obj_set_size(btn_recents, 60, 26);
  lv_obj_set_style_bg_opa(btn_recents, LV_OPA_TRANSP, 0);
  lv_obj_t *lbl_recents = lv_label_create(btn_recents);
  lv_label_set_text(lbl_recents, "Recents");
  lv_obj_center(lbl_recents);
  lv_obj_add_event_cb(btn_recents, nav_recents_cb, LV_EVENT_CLICKED, NULL);
}

lv_obj_t *wm_create_window(lv_obj_t *parent, const char *title) {
  lv_obj_t *target_parent = parent ? parent : app_container;
  ESP_LOGI(TAG, "[WM_DBG] Creating window on parent: %p (app_container: %p)",
           target_parent, app_container);

  lv_obj_t *win = lv_obj_create(target_parent);
  if (!win) {
    ESP_LOGE(TAG, "[WM_DBG] Failed to create LVGL window object!");
    return NULL;
  }

  lv_obj_remove_style_all(win);
  lv_obj_set_size(win, 320, 372);
  lv_obj_set_style_bg_color(win, lv_color_hex(0x121212), 0);
  lv_obj_set_style_bg_opa(win, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(win, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(win, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(win, 8, 0);
  lv_obj_set_style_pad_gap(win, 8, 0);

  if (title) {
    wm_add_text(win, title);
  }
  ESP_LOGI(TAG, "[WM_DBG] Window created successfully: %p", win);
  return win;
}

lv_obj_t *wm_create_scroll_list(lv_obj_t *parent, int32_t w, int32_t h) {
  lv_obj_t *list = lv_list_create(parent);
  lv_obj_set_size(list, w, h);
  lv_obj_set_style_bg_color(list, lv_color_hex(0x1a1a1a), 0);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scroll_snap_y(list, LV_SCROLL_SNAP_NONE);

  lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLL_MOMENTUM);

  return list;
}

lv_obj_t *wm_add_text(lv_obj_t *parent, const char *text) {
  lv_obj_t *lbl = lv_label_create(parent);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
  return lbl;
}

lv_obj_t *wm_add_button(lv_obj_t *parent, const char *label_text, int32_t w,
                        int32_t h) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, w, h);

  lv_obj_add_flag(btn, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x00E5FF), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x0088A3), LV_STATE_PRESSED);

  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, label_text);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0x000000), 0);
  lv_obj_center(lbl);

  return btn;
}

static void input_focus_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
    wm_keyboard_attach(lv_event_get_target(e));
  }
}

lv_obj_t *wm_add_input(lv_obj_t *parent, const char *placeholder,
                       bool is_password) {
  lv_obj_t *ta = lv_textarea_create(parent);
  lv_obj_set_size(ta, 240, 40);
  lv_textarea_set_password_mode(ta, is_password);
  lv_textarea_set_placeholder_text(ta, placeholder);

  lv_obj_add_event_cb(ta, input_focus_cb, LV_EVENT_PRESSED, NULL);
  return ta;
}

static wm_event_ctx_t *get_or_create_ctx(lv_obj_t *obj) {
  wm_event_ctx_t *ctx = (wm_event_ctx_t *)lv_obj_get_user_data(obj);
  if (!ctx) {
    ctx = (wm_event_ctx_t *)calloc(1, sizeof(wm_event_ctx_t));
    lv_obj_set_user_data(obj, ctx);
    lv_obj_add_event_cb(obj, global_event_dispatcher, LV_EVENT_ALL, ctx);
  }
  return ctx;
}

void wm_on_tap(lv_obj_t *obj, wm_action_cb_t cb, void *user_data) {
  wm_event_ctx_t *ctx = get_or_create_ctx(obj);
  ctx->tap_cb = cb;
  ctx->user_data = user_data;
}

void wm_on_hold(lv_obj_t *obj, wm_action_cb_t cb, void *user_data) {
  wm_event_ctx_t *ctx = get_or_create_ctx(obj);
  ctx->hold_cb = cb;
  ctx->user_data = user_data;
}

void wm_on_scroll(lv_obj_t *obj, wm_action_cb_t cb, void *user_data) {
  wm_event_ctx_t *ctx = get_or_create_ctx(obj);
  ctx->scroll_cb = cb;
  ctx->user_data = user_data;
}

// Toast Notifications
static void toast_timer_cb(lv_timer_t *timer) {
  lv_obj_t *toast = (lv_obj_t *)lv_timer_get_user_data(timer);
  if (toast && lv_obj_is_valid(toast)) {
    lv_obj_del(toast);
  }
}

static void toast_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *toast = lv_event_get_target(e);

  // Swipe gestures to dismiss
  if (code == LV_EVENT_GESTURE) {
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_TOP || dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) {
      lv_obj_del(toast);
    }
  }
}

void wm_toast(const char *message, uint32_t duration_ms) {
  lv_obj_t *toast = lv_obj_create(lv_layer_top());
  lv_obj_set_size(toast, 260, 36);
  lv_obj_align(toast, LV_ALIGN_TOP_MID, 0, 28);

  lv_obj_set_style_bg_color(toast, lv_color_hex(0x222222), 0);
  lv_obj_set_style_border_color(toast, lv_color_hex(0x00E5FF), 0);
  lv_obj_set_style_border_width(toast, 1, 0);
  lv_obj_set_style_radius(toast, 18, 0);

  lv_obj_clear_flag(toast, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lbl = lv_label_create(toast);
  lv_label_set_text(lbl, message);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(lbl);

  lv_obj_add_event_cb(toast, toast_event_cb, LV_EVENT_GESTURE, NULL);

  lv_timer_create(toast_timer_cb, duration_ms, toast);
}

// BASE OS ENGINE
static lv_obj_t *update_btn = NULL;
static lv_obj_t *wifi_icon_label = NULL;

static void update_btn_click_cb(lv_event_t *e) {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, "VantageOS");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -50);

  lv_obj_t *spinner = lv_spinner_create(scr);
  lv_spinner_set_anim_params(spinner, 1000, 60);
  lv_obj_set_size(spinner, 50, 50);
  lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 10);

  lv_scr_load(scr);
  execute_sd_ota_flash();
}

void wm_check_update_ui_state(void) {
  if (ota_is_update_available() && update_btn == NULL) {
    update_btn = lv_btn_create(status_bar);
    lv_obj_set_size(update_btn, 65, 18);
    lv_obj_align(update_btn, LV_ALIGN_RIGHT_MID, -35, 0);
    lv_obj_set_style_bg_color(update_btn, lv_palette_main(LV_PALETTE_BLUE), 0);

    lv_obj_t *lbl = lv_label_create(update_btn);
    lv_label_set_text(lbl, "UPDATE");
    lv_obj_center(lbl);

    lv_obj_add_event_cb(update_btn, update_btn_click_cb, LV_EVENT_CLICKED,
                        NULL);
  }
}

static void create_status_bar(lv_obj_t *parent) {
  status_bar = lv_obj_create(parent);
  lv_obj_remove_style_all(status_bar);
  lv_obj_set_size(status_bar, 320, 24);
  lv_obj_set_align(status_bar, LV_ALIGN_TOP_MID);
  lv_obj_set_style_bg_color(status_bar, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(status_bar, LV_OPA_80, 0);

  lv_obj_t *title = lv_label_create(status_bar);
  lv_label_set_text(title, "VANTAGE OS");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

  wifi_icon_label = lv_label_create(status_bar);
  lv_label_set_text(wifi_icon_label, "");
  lv_obj_set_style_text_color(wifi_icon_label, lv_color_hex(0x00E5FF), 0);
  lv_obj_align(wifi_icon_label, LV_ALIGN_RIGHT_MID, -8, 0);
}

void wm_update_wifi_status(bool connected) {
  if (!wifi_icon_label)
    return;
  lv_label_set_text(wifi_icon_label, connected ? LV_SYMBOL_WIFI : "");
}

static SemaphoreHandle_t lvgl_mutex = NULL;

void wm_lvgl_lock(void) {
  if (lvgl_mutex) {
    xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
  }
}

void wm_lvgl_unlock(void) {
  if (lvgl_mutex) {
    xSemaphoreGive(lvgl_mutex);
  }
}

static void crash_log_dismiss_cb(lv_obj_t *btn, void *user_data) {
  lv_obj_t *modal = (lv_obj_t *)user_data;
  if (modal && lv_obj_is_valid(modal)) {
    lv_obj_del(modal);
  }
}

static void check_and_show_crash_log(void) {
  esp_reset_reason_t reason = esp_reset_reason();

  bool was_crash = (reason == ESP_RST_PANIC || reason == ESP_RST_TASK_WDT ||
                    reason == ESP_RST_INT_WDT || reason == ESP_RST_BROWNOUT);

  if (rtc_crash_data.magic != CRASH_MAGIC && !was_crash) {
    return;
  }

  rtc_crash_data.magic = 0;

  if (strlen(rtc_crash_data.panic_msg) == 0) {
    switch (reason) {
    case ESP_RST_TASK_WDT:
      snprintf(rtc_crash_data.panic_msg, sizeof(rtc_crash_data.panic_msg),
               "Task Watchdog Reset Triggered!");
      break;
    case ESP_RST_INT_WDT:
      snprintf(rtc_crash_data.panic_msg, sizeof(rtc_crash_data.panic_msg),
               "Interrupt Watchdog Reset Triggered!");
      break;
    case ESP_RST_BROWNOUT:
      snprintf(rtc_crash_data.panic_msg, sizeof(rtc_crash_data.panic_msg),
               "Brownout Reset (Power Drop) Detected!");
      break;
    default:
      snprintf(rtc_crash_data.panic_msg, sizeof(rtc_crash_data.panic_msg),
               "System Crash / Reboot Detected!");
      break;
    }
  }

  lv_obj_t *modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(modal, 300, 380);
  lv_obj_center(modal);
  lv_obj_set_style_bg_color(modal, lv_color_hex(0x1a1a1a), 0);
  lv_obj_set_style_border_color(modal, lv_color_hex(0xFF0055), 0);
  lv_obj_set_style_border_width(modal, 2, 0);

  lv_obj_t *title = wm_add_text(modal, "CRASH DETECTED");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFF0055), 0);

  lv_obj_t *ta = lv_textarea_create(modal);
  lv_obj_set_size(ta, 270, 240);
  lv_obj_align(ta, LV_ALIGN_CENTER, 0, -10);
  lv_textarea_set_text(ta, rtc_crash_data.panic_msg);

  // Clear buffer for next time
  memset(rtc_crash_data.panic_msg, 0, sizeof(rtc_crash_data.panic_msg));

  lv_obj_t *btn = wm_add_button(modal, "Dismiss", 120, 36);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -5);
  wm_on_tap(btn, crash_log_dismiss_cb, modal);
}

void wm_init(void) {
  if (!lvgl_mutex) {
    lvgl_mutex = xSemaphoreCreateMutex();
  }

  esp_elf_register_symbol(host_symbols);

  main_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x0a0a0a), 0);

  app_container = lv_obj_create(main_screen);
  lv_obj_remove_style_all(app_container);
  lv_obj_set_size(app_container, 320, 426); // Leaves space for bottom nav bar
  lv_obj_align(app_container, LV_ALIGN_TOP_MID, 0, 24);

  create_status_bar(main_screen);
  create_navigation_bar(main_screen);
  create_system_keyboard();

  lv_scr_load(main_screen);
  wm_show_home();
  check_and_show_crash_log();
}

void wm_open_app(const app_descriptor_t *app, app_control_block_t *process) {
  if (!app) {
    ESP_LOGE(TAG, "[WM_DBG] wm_open_app received NULL app descriptor!");
    return;
  }

  // Restore focus if app is already open
  window_node_t *curr = window_stack;
  while (curr) {
    if (curr->app && curr->app->id && app->id &&
        strcmp(curr->app->id, app->id) == 0) {
      ESP_LOGI(
          TAG,
          "[WM_DBG] App %s already running. Bringing root_view %p to front.",
          app->title, curr->root_view);
      curr->is_minimized = false;
      lv_obj_clear_flag(curr->root_view, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(curr->root_view);
      return;
    }
    curr = curr->prev;
  }

  ESP_LOGI(TAG, "[WM_DBG] Opening New App: %s (ID: %s)", app->title, app->id);

  window_node_t *node = (window_node_t *)malloc(sizeof(window_node_t));
  if (!node) {
    ESP_LOGE(TAG, "[WM_DBG] Failed to allocate window node context");
    return;
  }
  node->app = app;
  node->process_handle = process;
  node->is_minimized = false;
  node->prev = window_stack;

  node->root_view = lv_obj_create(app_container);
  if (!node->root_view) {
    ESP_LOGE(TAG,
             "[WM_DBG] Failed to create root_view container on app_container!");
    free(node);
    return;
  }

  lv_obj_remove_style_all(node->root_view);
  lv_obj_set_size(node->root_view, 320, 426);
  lv_obj_set_style_bg_color(node->root_view, lv_color_hex(0x121212), 0);
  lv_obj_set_style_bg_opa(node->root_view, LV_OPA_COVER, 0);

  if (window_stack && window_stack->root_view) {
    ESP_LOGI(TAG, "[WM_DBG] Hiding previous active root_view: %p",
             window_stack->root_view);
    lv_obj_add_flag(window_stack->root_view, LV_OBJ_FLAG_HIDDEN);
  }

  window_stack = node;

  if (app->payload) {
    ESP_LOGI(TAG, "[WM_DBG] Setting user_data %p onto root_view %p",
             app->payload, node->root_view);
    lv_obj_set_user_data(node->root_view, app->payload);
  } else {
    ESP_LOGW(TAG, "[WM_DBG] Warning: App descriptor payload is NULL!");
  }

  if (app->on_create) {
    ESP_LOGI(TAG, "[WM_DBG] Calling app->on_create(%p)...", node->root_view);
    app->on_create(node->root_view);

    lv_obj_clear_flag(node->root_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(node->root_view);

    ESP_LOGI(TAG, "[WM_DBG] app->on_create finished.");
  }
}

void wm_close_current(void) {
  if (!window_stack)
    return;

  if (window_stack->app &&
      strcmp(window_stack->app->id, "com.vantage.home") == 0) {
    return;
  }

  window_node_t *top = window_stack;
  window_stack = top->prev;

  ESP_LOGI(TAG, "Closing App: %s", top->app->title);

  if (top->app->on_destroy) {
    top->app->on_destroy(top->root_view);
  }

  if (top->app->payload) {
    sdk_app_instance_t *inst = (sdk_app_instance_t *)top->app->payload;
    if (inst->payload) {
      heap_caps_free(inst->payload);
    }
    if (inst->sdk_app) {
      free((void *)inst->sdk_app);
    }
    free(inst);
    free((void *)top->app->id);
    free((void *)top->app->title);
    free((void *)top->app);
  }

  if (top->app) {
    kernel_terminate_app(top->process_handle);
    lv_obj_del(top->root_view);
  }

  free(top);

  if (window_stack && window_stack->root_view) {
    window_stack->is_minimized = false;
    lv_obj_clear_flag(window_stack->root_view, LV_OBJ_FLAG_HIDDEN);
  }
}

void wm_minimize_current(void) {
  if (!window_stack)
    return;

  // Prevent minimizing if already on Home Screen
  if (window_stack->app &&
      strcmp(window_stack->app->id, "com.vantage.home") == 0) {
    return;
  }

  // Hide keyboard if active
  wm_keyboard_hide();

  // Minimize current running app
  window_stack->is_minimized = true;
  lv_obj_add_flag(window_stack->root_view, LV_OBJ_FLAG_HIDDEN);

  // Find Home app in the stack and restore focus to it
  window_node_t *curr = window_stack;
  while (curr) {
    if (curr->app && strcmp(curr->app->id, "com.vantage.home") == 0) {
      curr->is_minimized = false;
      lv_obj_clear_flag(curr->root_view, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(curr->root_view);
      break;
    }
    curr = curr->prev;
  }

  wm_toast("App Minimized", 1000);
}

static lv_obj_t *app_switcher_modal = NULL;

static void switcher_item_click_cb(lv_event_t *e) {
  window_node_t *target_node = (window_node_t *)lv_event_get_user_data(e);

  window_node_t *curr = window_stack;
  while (curr) {
    curr->is_minimized = true;
    if (curr->root_view) {
      lv_obj_add_flag(curr->root_view, LV_OBJ_FLAG_HIDDEN);
    }
    curr = curr->prev;
  }

  if (target_node) {
    target_node->is_minimized = false;
    lv_obj_clear_flag(target_node->root_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(target_node->root_view);
  }

  if (app_switcher_modal && lv_obj_is_valid(app_switcher_modal)) {
    lv_obj_del(app_switcher_modal);
    app_switcher_modal = NULL;
  }
}

void wm_show_app_switcher(void) {
  wm_keyboard_hide();

  if (app_switcher_modal && lv_obj_is_valid(app_switcher_modal)) {
    lv_obj_del(app_switcher_modal);
    app_switcher_modal = NULL;
    return;
  }

  app_switcher_modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(app_switcher_modal, 300, 360);
  lv_obj_center(app_switcher_modal);
  lv_obj_set_style_bg_color(app_switcher_modal, lv_color_hex(0x181818), 0);
  lv_obj_set_style_border_color(app_switcher_modal, lv_color_hex(0x00E5FF), 0);
  lv_obj_set_style_border_width(app_switcher_modal, 1, 0);

  // Disable main modal container scrolling completely
  lv_obj_clear_flag(app_switcher_modal, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(app_switcher_modal);
  lv_label_set_text(title, "Active Applications");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

  lv_obj_t *list = lv_list_create(app_switcher_modal);
  lv_obj_set_size(list, 260, 280);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -5);

  // Clean up recents list scrolling (no momentum, no over-scroll bounce)
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLL_MOMENTUM);

  window_node_t *curr = window_stack;
  while (curr) {
    if (curr->app && curr->app->hide_from_recents) {
      curr = curr->prev;
      continue;
    }

    lv_obj_t *btn = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, curr->app->title);
    lv_obj_add_event_cb(btn, switcher_item_click_cb, LV_EVENT_CLICKED, curr);
    curr = curr->prev;
  }
}

typedef struct {
  char app_id[64];
  char app_name[64];
  char base_path[32];
  char full_elf_path[128];
} home_app_ctx_t;

static void home_uninstall_confirm_cb(lv_obj_t *btn, void *user_data) {
  home_app_ctx_t *act = (home_app_ctx_t *)user_data;

  // Purge app from disk and registry
  app_registry_remove(act->base_path, act->app_id);

  wm_toast("App Uninstalled", 1500);
  wm_reload_home();

  // Close modal overlay
  lv_obj_t *modal = lv_obj_get_parent(btn);
  if (modal)
    lv_obj_del(modal);
  free(act);
}

static void home_app_hold_cb(lv_obj_t *obj, void *user_data) {
  home_app_ctx_t *act = (home_app_ctx_t *)user_data;

  lv_obj_t *modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(modal, 260, 160);
  lv_obj_center(modal);
  lv_obj_set_style_bg_color(modal, lv_color_hex(0x1a1a1a), 0);
  lv_obj_set_style_border_color(modal, lv_color_hex(0x00E5FF), 0);

  char prompt[128];
  snprintf(prompt, sizeof(prompt), "Uninstall %s?", act->app_name);
  wm_add_text(modal, prompt);

  lv_obj_t *btn_del = wm_add_button(modal, "Uninstall", 200, 36);
  lv_obj_set_style_bg_color(btn_del, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_align(btn_del, LV_ALIGN_BOTTOM_MID, 0, -10);

  wm_on_tap(btn_del, home_uninstall_confirm_cb, act);
}

typedef struct {
  char ssid[33];
  lv_obj_t *pwd_ta;
  lv_obj_t *modal;
} wifi_connect_ctx_t;

static void modal_connect_cb(lv_obj_t *btn, void *user_data) {
  wifi_connect_ctx_t *ctx = (wifi_connect_ctx_t *)user_data;
  const char *pwd = lv_textarea_get_text(ctx->pwd_ta);

  wifi_manager_connect(ctx->ssid, pwd);
  wifi_save_credentials(ctx->ssid, pwd);

  wm_toast("Connecting to Wi-Fi...", 2000);
  wm_keyboard_hide();
  lv_obj_del(ctx->modal);
  free(ctx);
}

static void modal_cancel_cb(lv_obj_t *btn, void *user_data) {
  wifi_connect_ctx_t *ctx = (wifi_connect_ctx_t *)user_data;
  wm_keyboard_hide();
  lv_obj_del(ctx->modal);
  free(ctx);
}

static void wifi_item_click_cb(lv_obj_t *obj, void *user_data) {
  const char *ssid = (const char *)user_data;

  lv_obj_t *modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(modal, 280, 180);
  lv_obj_center(modal);
  lv_obj_set_style_bg_color(modal, lv_color_hex(0x1a1a1a), 0);
  lv_obj_set_style_border_color(modal, lv_color_hex(0x00E5FF), 0);

  wm_add_text(modal, ssid);

  lv_obj_t *pwd_ta = wm_add_input(modal, "Enter Password", true);
  lv_obj_align(pwd_ta, LV_ALIGN_CENTER, 0, -10);

  wifi_connect_ctx_t *ctx =
      (wifi_connect_ctx_t *)malloc(sizeof(wifi_connect_ctx_t));
  strncpy(ctx->ssid, ssid, 32);
  ctx->pwd_ta = pwd_ta;
  ctx->modal = modal;

  lv_obj_t *btn_conn = wm_add_button(modal, "Connect", 90, 32);
  lv_obj_align(btn_conn, LV_ALIGN_BOTTOM_RIGHT, -10, -5);
  wm_on_tap(btn_conn, modal_connect_cb, ctx);

  lv_obj_t *btn_canc = wm_add_button(modal, "Cancel", 90, 32);
  lv_obj_align(btn_canc, LV_ALIGN_BOTTOM_LEFT, 10, -5);
  wm_on_tap(btn_canc, modal_cancel_cb, ctx);
}

static void wifi_menu_create_cb(lv_obj_t *parent) {
  lv_obj_t *win = wm_create_window(parent, "Wi-Fi Settings");
  lv_obj_t *list = wm_create_scroll_list(win, 300, 360);

  wifi_scan_entry_t *scan_results = NULL;
  uint16_t count = wifi_manager_scan(&scan_results);

  for (int i = 0; i < count; i++) {
    if (strlen(scan_results[i].ssid) == 0)
      continue;

    char item_text[64];
    snprintf(item_text, sizeof(item_text), "%s (%d dBm)", scan_results[i].ssid,
             scan_results[i].rssi);

    lv_obj_t *btn = lv_list_add_btn(list, LV_SYMBOL_WIFI, item_text);
    char *ssid_copy = strdup(scan_results[i].ssid);

    wm_on_tap(btn, wifi_item_click_cb, ssid_copy);
  }

  if (scan_results)
    free(scan_results);
}

const app_descriptor_t wifi_app = {.id = "com.vantage.wifi",
                                   .title = "Wi-Fi Settings",
                                   .on_create = wifi_menu_create_cb,
                                   .on_destroy = NULL};

static void on_wifi_btn_tap(lv_obj_t *obj, void *user_data) {
  wm_open_app(&wifi_app, NULL);
}

static void on_fm_btn_tap(lv_obj_t *obj, void *user_data) {
  wm_open_app(&file_manager_app, NULL);
}

static lv_obj_t *home_apps_container = NULL;

static void dynamic_app_click_cb(lv_obj_t *obj, void *user_data) {
  home_app_ctx_t *act = (home_app_ctx_t *)user_data;
  if (!act || strlen(act->full_elf_path) == 0)
    return;

  app_launcher_exec_elf(act->full_elf_path);
}

void load_dynamic_apps_from_registry(lv_obj_t *container,
                                     const char *base_path) {
  char reg_path[128];
  snprintf(reg_path, sizeof(reg_path), "%s/apps/installed_apps.json",
           base_path);

  FILE *f = fopen(reg_path, "rb");
  if (!f)
    return;

  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *buf = (char *)malloc(sz + 1);
  if (!buf) {
    fclose(f);
    return;
  }

  fread(buf, 1, sz, f);
  fclose(f);
  buf[sz] = '\0';

  cJSON *root = cJSON_Parse(buf);
  free(buf);

  if (!root || !cJSON_IsArray(root)) {
    if (root)
      cJSON_Delete(root);
    return;
  }

  int app_count = cJSON_GetArraySize(root);
  for (int i = 0; i < app_count; i++) {
    cJSON *app_item = cJSON_GetArrayItem(root, i);
    cJSON *name = cJSON_GetObjectItem(app_item, "name");
    cJSON *id = cJSON_GetObjectItem(app_item, "id");
    cJSON *exec = cJSON_GetObjectItem(app_item, "exec");

    if (cJSON_IsString(name) && cJSON_IsString(id)) {
      home_app_ctx_t *act = (home_app_ctx_t *)calloc(1, sizeof(home_app_ctx_t));
      const char *exec_name =
          (cJSON_IsString(exec) && strlen(exec->valuestring) > 0)
              ? exec->valuestring
              : "app.elf";

      strncpy(act->app_id, id->valuestring, sizeof(act->app_id) - 1);
      strncpy(act->app_name, name->valuestring, sizeof(act->app_name) - 1);
      strncpy(act->base_path, base_path, sizeof(act->base_path) - 1);

      char direct_file_path[256];
      char subfolder_file_path[256];

      snprintf(direct_file_path, sizeof(direct_file_path), "%s/apps/%s",
               base_path, id->valuestring);
      snprintf(subfolder_file_path, sizeof(subfolder_file_path),
               "%s/apps/%s/%s", base_path, id->valuestring, exec_name);

      struct stat st;
      if (stat(subfolder_file_path, &st) == 0) {
        strncpy(act->full_elf_path, subfolder_file_path,
                sizeof(act->full_elf_path) - 1);
      } else if (stat(direct_file_path, &st) == 0 && !S_ISDIR(st.st_mode)) {
        strncpy(act->full_elf_path, direct_file_path,
                sizeof(act->full_elf_path) - 1);
      } else {
        app_registry_remove(base_path, id->valuestring);
        free(act);
        continue;
      }

      lv_obj_t *btn = add_grid_app_item(container, name->valuestring,
                                        LV_SYMBOL_FILE, MAX_APP_NAME_CHARS);
      wm_on_tap(btn, dynamic_app_click_cb, act);
      wm_on_hold(btn, home_app_hold_cb, act);
    }
  }

  cJSON_Delete(root);
}

void wm_reload_home(void) {
  if (!home_apps_container || !lv_obj_is_valid(home_apps_container))
    return;

  lv_obj_clean(home_apps_container);

  lv_obj_t *btn_wifi = add_grid_app_item(home_apps_container, "Wi-Fi Settings",
                                         LV_SYMBOL_WIFI, 8);
  wm_on_tap(btn_wifi, on_wifi_btn_tap, NULL);

  lv_obj_t *btn_fm = add_grid_app_item(home_apps_container, "File Manager",
                                       LV_SYMBOL_DIRECTORY, 8);
  wm_on_tap(btn_fm, on_fm_btn_tap, NULL);

  load_dynamic_apps_from_registry(home_apps_container, "/int");
  load_dynamic_apps_from_registry(home_apps_container, "/sdcard");
}

static void home_create_cb(lv_obj_t *parent) {
  lv_obj_t *win = wm_create_window(parent, NULL);
  home_apps_container = lv_obj_create(win);
  lv_obj_remove_style_all(home_apps_container);
  lv_obj_set_size(home_apps_container, 304, 380);
  lv_obj_set_flex_flow(home_apps_container, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(home_apps_container, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_gap(home_apps_container, 8, 0);

  // Load registered apps
  wm_reload_home();
}

static const app_descriptor_t home_app = {.id = "com.vantage.home",
                                          .title = "Home",
                                          .on_create = home_create_cb,
                                          .on_destroy = NULL};

void wm_show_home(void) {
  while (window_stack != NULL) {
    wm_close_current();
  }
  wm_open_app(&home_app, NULL);
}

void wm_register_sdk_app(const vantage_app_desc_t *sdk_app, void *payload) {
  if (!sdk_app)
    return;

  // Convert SDK descriptor to OS descriptor
  app_descriptor_t *os_app =
      sdk_bridge_create_os_app(sdk_app, "/sdcard", payload);
  if (!os_app)
    return;

  // Launch app through OS Window Manager
  wm_open_app(os_app, NULL);
}