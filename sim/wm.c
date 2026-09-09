#include "wm.h"
#include "wm_anim.h"
#include "sdk_bridge.h"
#include "host_wifi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define SIM_ROOT "sim_data"
#define SCREEN_W 320
#define SCREEN_H 480
#define CONTENT_H 426
#define PATH_LEN 512

/*
Look the other way if you don't want to sell your soul to the devil.
*/

typedef struct wm_event_ctx {
  wm_action_cb_t tap_cb;
  wm_action_cb_t hold_cb;
  wm_action_cb_t scroll_cb;
  void *user_data;
  bool hold_triggered;
} wm_event_ctx_t;

typedef struct window_node {
  const app_descriptor_t *app;
  lv_obj_t *root_view;
  struct window_node *prev;
  struct window_node *next;
} window_node_t;

typedef struct {
  char virtual_path[PATH_LEN];
  lv_obj_t *path_label;
  lv_obj_t *list;
} fm_context_t;

typedef struct {
  fm_context_t *fm;
  char name[256];
  bool is_dir;
} fm_entry_t;

typedef struct {
  fm_context_t *fm;
  char name[256];
  lv_obj_t *modal;
  lv_obj_t *input;
  bool make_dir;
} fm_modal_ctx_t;

typedef struct {
  fm_context_t *fm;
  char host_path[PATH_LEN];
  lv_obj_t *modal;
  lv_obj_t *rename_input;
} fm_action_ctx_t;

typedef struct {
  char host_path[PATH_LEN];
  lv_obj_t *modal;
  lv_obj_t *text_area;
} editor_ctx_t;

typedef struct {
  char package_name[256];
  lv_obj_t *modal;
} package_install_ctx_t;

static lv_obj_t *main_screen;
static lv_obj_t *status_bar;
static lv_obj_t *app_container;
static lv_obj_t *sys_keyboard;
static lv_obj_t *wifi_icon;
static lv_obj_t *home_grid;
static lv_obj_t *recents_modal;
static window_node_t *window_list;
static window_node_t *active_window;
static lv_obj_t *launch_source;
static char connected_ssid[33];

static void home_create(lv_obj_t *parent);
static void wifi_create(lv_obj_t *parent);
static void files_create(lv_obj_t *parent);
static void files_destroy(lv_obj_t *parent);
static void settings_create(lv_obj_t *parent);
static void package_create(lv_obj_t *parent);
static bool host_is_directory(const char *path);

static const app_descriptor_t home_app = {
    "com.vantage.home", "Home", true, home_create, NULL, NULL};
static const app_descriptor_t wifi_app = {
    "com.vantage.wifi", "Wi-Fi Settings", false, wifi_create, NULL, NULL};
static const app_descriptor_t files_app = {"com.vantage.filemanager",
                                           "File Manager", false,
                                           files_create, files_destroy, NULL};
static const app_descriptor_t settings_app = {"com.vantage.settings",
                                              "System Settings", false,
                                              settings_create, NULL, NULL};
static const app_descriptor_t package_app = {"com.vantage.package-demo",
                                             "Installed VPK App", false,
                                             package_create, NULL, NULL};

const char *wm_sim_data_root(void) { return SIM_ROOT; }

static void make_dir(const char *path) {
#ifdef _WIN32
  _mkdir(path);
#else
  mkdir(path, 0775);
#endif
}

static void sim_init_storage(void) {
  make_dir(SIM_ROOT);
  make_dir(SIM_ROOT "/int");
  make_dir(SIM_ROOT "/int/apps");
  make_dir(SIM_ROOT "/sdcard");
  make_dir(SIM_ROOT "/sdcard/apps");
  make_dir(SIM_ROOT "/sdcard/system");
  make_dir(SIM_ROOT "/system");
}

static void virtual_to_host(const char *virtual_path, char *out,
                            size_t out_size) {
  if (!virtual_path || strcmp(virtual_path, "/") == 0) {
    snprintf(out, out_size, "%s", SIM_ROOT);
  } else {
    snprintf(out, out_size, "%s%s", SIM_ROOT, virtual_path);
  }
}

static void global_event_dispatcher(lv_event_t *event) {
  wm_event_ctx_t *ctx = lv_event_get_user_data(event);
  if (!ctx)
    return;
  lv_event_code_t code = lv_event_get_code(event);
  lv_obj_t *target = lv_event_get_target(event);
  if (code == LV_EVENT_PRESSED) {
    ctx->hold_triggered = false;
  } else if (code == LV_EVENT_LONG_PRESSED) {
    ctx->hold_triggered = true;
    if (ctx->hold_cb)
      ctx->hold_cb(target, ctx->user_data);
  } else if (code == LV_EVENT_CLICKED) {
    if (ctx->hold_triggered) {
      ctx->hold_triggered = false;
      return;
    }
    if (ctx->tap_cb)
      ctx->tap_cb(target, ctx->user_data);
  } else if (code == LV_EVENT_SCROLL && ctx->scroll_cb) {
    ctx->scroll_cb(target, ctx->user_data);
  }
}

static wm_event_ctx_t *event_context(lv_obj_t *obj) {
  wm_event_ctx_t *ctx = lv_obj_get_user_data(obj);
  if (!ctx) {
    ctx = calloc(1, sizeof(*ctx));
    lv_obj_set_user_data(obj, ctx);
    lv_obj_add_event_cb(obj, global_event_dispatcher, LV_EVENT_ALL, ctx);
  }
  return ctx;
}

void wm_on_tap(lv_obj_t *obj, wm_action_cb_t cb, void *user_data) {
  wm_event_ctx_t *ctx = event_context(obj);
  ctx->tap_cb = cb;
  ctx->user_data = user_data;
}

void wm_on_hold(lv_obj_t *obj, wm_action_cb_t cb, void *user_data) {
  wm_event_ctx_t *ctx = event_context(obj);
  ctx->hold_cb = cb;
  ctx->user_data = user_data;
}

void wm_on_scroll(lv_obj_t *obj, wm_action_cb_t cb, void *user_data) {
  wm_event_ctx_t *ctx = event_context(obj);
  ctx->scroll_cb = cb;
  ctx->user_data = user_data;
}

static void set_opa(void *var, int32_t value) {
  lv_obj_set_style_opa(var, (lv_opa_t)value, 0);
}

static void toast_timer(lv_timer_t *timer) {
  lv_obj_t *toast = lv_timer_get_user_data(timer);
  if (toast && lv_obj_is_valid(toast)) {
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, toast);
    lv_anim_set_values(&animation, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&animation, 130);
    lv_anim_set_exec_cb(&animation, set_opa);
    lv_anim_start(&animation);
    lv_obj_delete_delayed(toast, 140);
  }
}

void wm_toast(const char *message, uint32_t duration_ms) {
  lv_obj_t *toast = lv_obj_create(lv_layer_top());
  lv_obj_set_size(toast, 272, 38);
  lv_obj_align(toast, LV_ALIGN_TOP_MID, 0, 30);
  lv_obj_set_style_bg_color(toast, lv_color_hex(0x1B252B), 0);
  lv_obj_set_style_bg_opa(toast, LV_OPA_90, 0);
  lv_obj_set_style_border_color(toast, lv_color_hex(0x32D9FF), 0);
  lv_obj_set_style_border_width(toast, 1, 0);
  lv_obj_set_style_radius(toast, 19, 0);
  lv_obj_set_style_shadow_color(toast, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_width(toast, 10, 0);
  lv_obj_clear_flag(toast, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *label = wm_add_text(toast, message);
  lv_obj_center(label);
  lv_obj_set_style_opa(toast, LV_OPA_TRANSP, 0);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, toast);
  lv_anim_set_values(&animation, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&animation, 140);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, set_opa);
  lv_anim_start(&animation);
  lv_timer_t *timer = lv_timer_create(toast_timer, duration_ms, toast);
  lv_timer_set_repeat_count(timer, 1);
}

lv_obj_t *wm_add_text(lv_obj_t *parent, const char *text) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text ? text : "");
  lv_obj_set_style_text_color(label, lv_color_hex(0xF2F7FA), 0);
  return label;
}

lv_obj_t *wm_add_button(lv_obj_t *parent, const char *label, int32_t width,
                        int32_t height) {
  lv_obj_t *button = lv_btn_create(parent);
  lv_obj_set_size(button, width, height);
  lv_obj_add_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_set_style_radius(button, 10, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x24C8EE), 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x15829C), LV_STATE_PRESSED);
  lv_obj_set_style_translate_y(button, 1, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_color(button, lv_color_hex(0x005669), 0);
  lv_obj_set_style_shadow_width(button, 5, 0);
  lv_obj_set_style_shadow_opa(button, LV_OPA_40, 0);
  lv_obj_t *text = lv_label_create(button);
  lv_label_set_text(text, label);
  lv_obj_set_style_text_color(text, lv_color_hex(0x041417), 0);
  lv_obj_center(text);
  return button;
}

static void input_focus(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_PRESSED)
    wm_keyboard_attach(lv_event_get_target(event));
}

lv_obj_t *wm_add_input(lv_obj_t *parent, const char *placeholder,
                       bool is_password) {
  lv_obj_t *input = lv_textarea_create(parent);
  lv_obj_set_size(input, 244, 42);
  lv_textarea_set_placeholder_text(input, placeholder);
  lv_textarea_set_password_mode(input, is_password);
  lv_obj_set_style_bg_color(input, lv_color_hex(0x0D1418), 0);
  lv_obj_set_style_border_color(input, lv_color_hex(0x2C7180), 0);
  lv_obj_set_style_border_width(input, 1, 0);
  lv_obj_set_style_radius(input, 8, 0);
  lv_obj_add_event_cb(input, input_focus, LV_EVENT_PRESSED, NULL);
  return input;
}

lv_obj_t *wm_create_window(lv_obj_t *parent, const char *title) {
  lv_obj_t *window = lv_obj_create(parent ? parent : app_container);
  lv_obj_remove_style_all(window);
  lv_obj_set_size(window, SCREEN_W, CONTENT_H);
  lv_obj_set_style_bg_color(window, lv_color_hex(0x10171B), 0);
  lv_obj_set_style_bg_opa(window, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(window, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(window, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(window, 8, 0);
  lv_obj_set_style_pad_gap(window, 8, 0);
  if (title) {
    lv_obj_t *heading = wm_add_text(window, title);
    lv_obj_set_style_text_font(heading, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(heading, lv_color_hex(0xFFFFFF), 0);
  }
  return window;
}

lv_obj_t *wm_create_scroll_list(lv_obj_t *parent, int32_t width,
                                int32_t height) {
  lv_obj_t *list = lv_list_create(parent);
  lv_obj_set_size(list, width, height);
  lv_obj_set_style_bg_color(list, lv_color_hex(0x151F24), 0);
  lv_obj_set_style_radius(list, 10, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  return list;
}

static void keyboard_event(lv_event_t *event) {
  lv_obj_t *target = lv_keyboard_get_textarea(sys_keyboard);
  if (!target)
    return;
  if (lv_event_get_code(event) == LV_EVENT_READY ||
      lv_event_get_code(event) == LV_EVENT_CANCEL)
    wm_keyboard_hide();
}

void wm_keyboard_attach(lv_obj_t *text_area) {
  if (!sys_keyboard)
    return;
  lv_obj_move_foreground(sys_keyboard);
  lv_keyboard_set_textarea(sys_keyboard, text_area);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, sys_keyboard);
  lv_anim_set_values(&animation, lv_obj_get_y(sys_keyboard), 0);
  lv_anim_set_duration(&animation, 190);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, (lv_anim_exec_xcb_t)lv_obj_set_y);
  lv_anim_start(&animation);
}

void wm_keyboard_hide(void) {
  if (!sys_keyboard)
    return;
  lv_keyboard_set_textarea(sys_keyboard, NULL);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, sys_keyboard);
  lv_anim_set_values(&animation, lv_obj_get_y(sys_keyboard), 180);
  lv_anim_set_duration(&animation, 170);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_in);
  lv_anim_set_exec_cb(&animation, (lv_anim_exec_xcb_t)lv_obj_set_y);
  lv_anim_start(&animation);
}

void wm_update_wifi_status(bool connected) {
  if (wifi_icon)
    lv_label_set_text(wifi_icon, connected ? LV_SYMBOL_WIFI : "");
}

static window_node_t *find_window(const app_descriptor_t *app) {
  for (window_node_t *node = window_list; node; node = node->next)
    if (node->app == app)
      return node;
  return NULL;
}

void wm_open_app(const app_descriptor_t *app, void *process) {
  (void)process;
  if (!app)
    return;
  window_node_t *existing = find_window(app);
  if (existing) {
    if (active_window && active_window != existing)
      lv_obj_add_flag(active_window->root_view, LV_OBJ_FLAG_HIDDEN);
    active_window = existing;
    sim_sdk_focus_app(existing->app);
    lv_obj_clear_flag(existing->root_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(existing->root_view);
    return;
  }
  window_node_t *node = calloc(1, sizeof(*node));
  if (!node)
    return;
  node->app = app;
  node->root_view = lv_obj_create(app_container);
  lv_obj_remove_style_all(node->root_view);
  lv_obj_set_size(node->root_view, SCREEN_W, CONTENT_H);
  lv_obj_set_style_bg_color(node->root_view, lv_color_hex(0x10171B), 0);
  lv_obj_set_style_bg_opa(node->root_view, LV_OPA_COVER, 0);
  if (active_window)
    lv_obj_add_flag(active_window->root_view, LV_OBJ_FLAG_HIDDEN);
  if (!window_list) {
    window_list = node;
  } else {
    window_node_t *tail = window_list;
    while (tail->next)
      tail = tail->next;
    tail->next = node;
    node->prev = tail;
  }
  active_window = node;
  if (app->payload)
    lv_obj_set_user_data(node->root_view, app->payload);
  sim_sdk_focus_app(app);
  if (app->on_create)
    app->on_create(node->root_view);
  lv_obj_clear_flag(node->root_view, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(node->root_view);
  if (launch_source) {
    wm_animate_launch(launch_source, node->root_view);
    launch_source = NULL;
  }
}

void wm_open_app_animated(lv_obj_t *source, const app_descriptor_t *app,
                          void *process) {
  launch_source = source;
  wm_open_app(app, process);
  launch_source = NULL;
}

void wm_close_current(void) {
  if (!active_window || active_window->app == &home_app)
    return;
  window_node_t *closing = active_window;
  window_node_t *next = closing->prev ? closing->prev : closing->next;
  if (closing->app->on_destroy)
    closing->app->on_destroy(closing->root_view);
  if (closing->prev)
    closing->prev->next = closing->next;
  else
    window_list = closing->next;
  if (closing->next)
    closing->next->prev = closing->prev;
  lv_obj_del(closing->root_view);
  free(closing);
  active_window = next;
  if (active_window) {
    sim_sdk_focus_app(active_window->app);
    lv_obj_clear_flag(active_window->root_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(active_window->root_view);
  }
}

void wm_minimize_current(void) {
  if (!active_window || active_window->app == &home_app)
    return;
  wm_keyboard_hide();
  lv_obj_add_flag(active_window->root_view, LV_OBJ_FLAG_HIDDEN);
  window_node_t *home = find_window(&home_app);
  if (home) {
    active_window = home;
    sim_sdk_focus_app(home->app);
    lv_obj_clear_flag(home->root_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(home->root_view);
  }
}

static void recents_pick(lv_obj_t *object, void *user_data) {
  (void)object;
  window_node_t *selected = user_data;
  if (!selected)
    return;
  for (window_node_t *node = window_list; node; node = node->next)
    lv_obj_add_flag(node->root_view, LV_OBJ_FLAG_HIDDEN);
  active_window = selected;
  sim_sdk_focus_app(selected->app);
  lv_obj_clear_flag(selected->root_view, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(selected->root_view);
  if (recents_modal) {
    lv_obj_del(recents_modal);
    recents_modal = NULL;
  }
}

void wm_show_app_switcher(void) {
  wm_keyboard_hide();
  if (recents_modal) {
    lv_obj_del(recents_modal);
    recents_modal = NULL;
    return;
  }
  recents_modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(recents_modal, 292, 340);
  lv_obj_center(recents_modal);
  lv_obj_set_style_bg_color(recents_modal, lv_color_hex(0x172126), 0);
  lv_obj_set_style_border_color(recents_modal, lv_color_hex(0x25C8EF), 0);
  lv_obj_set_style_radius(recents_modal, 14, 0);
  lv_obj_t *title = wm_add_text(recents_modal, "Active Applications");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_t *list = wm_create_scroll_list(recents_modal, 252, 255);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -12);
  for (window_node_t *node = window_list; node; node = node->next) {
    if (node->app->hide_from_recents)
      continue;
    lv_obj_t *item = lv_list_add_btn(list, LV_SYMBOL_FILE, node->app->title);
    wm_on_tap(item, recents_pick, node);
  }
}

void wm_show_home(void) {
  wm_open_app(&home_app, NULL);
  window_node_t *home = find_window(&home_app);
  for (window_node_t *node = window_list; node; node = node->next)
    if (node != home)
      lv_obj_add_flag(node->root_view, LV_OBJ_FLAG_HIDDEN);
  active_window = home;
  sim_sdk_focus_app(home->app);
  lv_obj_clear_flag(home->root_view, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(home->root_view);
}

static void nav_back(lv_event_t *event) { wm_close_current(); }
static void nav_home(lv_event_t *event) { wm_minimize_current(); }
static void nav_recents(lv_event_t *event) { wm_show_app_switcher(); }

static void create_status_bar(lv_obj_t *parent) {
  status_bar = lv_obj_create(parent);
  lv_obj_remove_style_all(status_bar);
  lv_obj_set_size(status_bar, SCREEN_W, 24);
  lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(status_bar, lv_color_hex(0x071014), 0);
  lv_obj_set_style_bg_opa(status_bar, LV_OPA_COVER, 0);
  lv_obj_t *name = wm_add_text(status_bar, "VANTAGE OS  •  DESKTOP SIM");
  lv_obj_set_style_text_font(name, &lv_font_montserrat_10, 0);
  lv_obj_align(name, LV_ALIGN_LEFT_MID, 8, 0);
  wifi_icon = wm_add_text(status_bar, "");
  lv_obj_set_style_text_color(wifi_icon, lv_color_hex(0x38D7F9), 0);
  lv_obj_align(wifi_icon, LV_ALIGN_RIGHT_MID, -9, 0);
}

static void create_navigation(lv_obj_t *parent) {
  lv_obj_t *nav = lv_obj_create(parent);
  lv_obj_remove_style_all(nav);
  lv_obj_set_size(nav, SCREEN_W, 30);
  lv_obj_align(nav, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(nav, lv_color_hex(0x071014), 0);
  lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(nav, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_t *back = lv_btn_create(nav);
  lv_obj_set_size(back, 64, 26);
  lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
  lv_obj_t *back_text = wm_add_text(back, "Back");
  lv_obj_center(back_text);
  lv_obj_add_event_cb(back, nav_back, LV_EVENT_CLICKED, NULL);
  lv_obj_t *home = lv_btn_create(nav);
  lv_obj_set_size(home, 64, 26);
  lv_obj_set_style_bg_opa(home, LV_OPA_TRANSP, 0);
  lv_obj_t *home_text = wm_add_text(home, "Home");
  lv_obj_center(home_text);
  lv_obj_add_event_cb(home, nav_home, LV_EVENT_CLICKED, NULL);
  lv_obj_t *recents = lv_btn_create(nav);
  lv_obj_set_size(recents, 64, 26);
  lv_obj_set_style_bg_opa(recents, LV_OPA_TRANSP, 0);
  lv_obj_t *recent_text = wm_add_text(recents, "Recents");
  lv_obj_center(recent_text);
  lv_obj_add_event_cb(recents, nav_recents, LV_EVENT_CLICKED, NULL);
}

void wm_init(void) {
  sim_init_storage();
  main_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x0A1013), 0);
  app_container = lv_obj_create(main_screen);
  lv_obj_remove_style_all(app_container);
  lv_obj_set_size(app_container, SCREEN_W, CONTENT_H);
  lv_obj_align(app_container, LV_ALIGN_TOP_MID, 0, 24);
  create_status_bar(main_screen);
  create_navigation(main_screen);
  sys_keyboard = lv_keyboard_create(lv_layer_top());
  lv_obj_set_size(sys_keyboard, SCREEN_W, 180);
  lv_obj_align(sys_keyboard, LV_ALIGN_BOTTOM_MID, 0, 180);
  lv_obj_clear_flag(sys_keyboard, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(sys_keyboard, keyboard_event, LV_EVENT_ALL, NULL);
  lv_scr_load(main_screen);
  wm_show_home();
}

static lv_obj_t *add_grid_item(lv_obj_t *parent, const char *title,
                               const char *symbol) {
  lv_obj_t *cell = lv_obj_create(parent);
  lv_obj_remove_style_all(cell);
  lv_obj_set_size(cell, 68, 80);
  lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_gap(cell, 5, 0);
  lv_obj_t *button = lv_btn_create(cell);
  lv_obj_set_size(button, 54, 54);
  lv_obj_set_style_radius(button, 15, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x1BAFCF), 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x10677C), LV_STATE_PRESSED);
  lv_obj_t *icon = lv_label_create(button);
  lv_label_set_text(icon, symbol);
  lv_obj_set_style_text_color(icon, lv_color_hex(0xE8FBFF), 0);
  lv_obj_center(icon);
  lv_obj_t *label = wm_add_text(cell, title);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
  return button;
}

static void launch_app(lv_obj_t *obj, void *user_data) {
  wm_open_app_animated(obj, user_data, NULL);
}

static bool has_installed_package(void) {
  FILE *file = fopen(SIM_ROOT "/int/apps/installed_apps.txt", "rb");
  if (!file)
    return false;
  fclose(file);
  return true;
}

void wm_reload_home(void) {
  if (!home_grid || !lv_obj_is_valid(home_grid))
    return;
  lv_obj_clean(home_grid);
  lv_obj_t *wifi = add_grid_item(home_grid, "Wi-Fi", LV_SYMBOL_WIFI);
  wm_on_tap(wifi, launch_app, (void *)&wifi_app);
  lv_obj_t *files = add_grid_item(home_grid, "Files", LV_SYMBOL_DIRECTORY);
  wm_on_tap(files, launch_app, (void *)&files_app);
  lv_obj_t *settings = add_grid_item(home_grid, "System", LV_SYMBOL_SETTINGS);
  wm_on_tap(settings, launch_app, (void *)&settings_app);
  if (has_installed_package()) {
    lv_obj_t *package = add_grid_item(home_grid, "VPK App", LV_SYMBOL_FILE);
    wm_on_tap(package, launch_app, (void *)&package_app);
  }
  for (size_t index = 0; index < sim_portable_app_count(); ++index) {
    const app_descriptor_t *portable =
        sim_sdk_wrap_app(sim_portable_app_at(index));
    if (!portable)
      continue;
    lv_obj_t *app = add_grid_item(home_grid, portable->title, LV_SYMBOL_FILE);
    wm_on_tap(app, launch_app, (void *)portable);
  }
}

static void home_create(lv_obj_t *parent) {
  lv_obj_t *window = wm_create_window(parent, NULL);
  lv_obj_t *title = wm_add_text(window, "Your device, in a desktop window.");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xD7F8FF), 0);
  lv_obj_t *subtitle = wm_add_text(window,
      "Use the same app flows without connecting hardware.");
  lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(subtitle, lv_color_hex(0x91ABB3), 0);
  home_grid = lv_obj_create(window);
  lv_obj_remove_style_all(home_grid);
  lv_obj_set_size(home_grid, 300, 340);
  lv_obj_set_flex_flow(home_grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(home_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_gap(home_grid, 8, 0);
  wm_reload_home();
}

static void close_modal(lv_obj_t *object, void *user_data) {
  (void)object;
  lv_obj_t *modal = user_data;
  if (modal && lv_obj_is_valid(modal))
    lv_obj_del(modal);
}

static bool ask_windows_about_wifi(void) {
  memset(connected_ssid, 0, sizeof(connected_ssid));
  bool connected = sim_host_wifi_get_current(connected_ssid,
                                             sizeof(connected_ssid));
  wm_update_wifi_status(connected);
  return connected;
}

static void wifi_refresh(lv_obj_t *object, void *user_data) {
  (void)object;
  lv_obj_t *status_label = user_data;
  bool connected = ask_windows_about_wifi();
  char status[80];
  snprintf(status, sizeof(status), "Host Wi-Fi: %s",
           connected ? connected_ssid : "Not connected or unavailable");
  lv_label_set_text(status_label, status);
  lv_obj_set_style_text_color(status_label,
                              connected ? lv_color_hex(0x52E3AC)
                                        : lv_color_hex(0xA4B1B6),
                              0);
  wm_toast(connected ? "Host Wi-Fi status refreshed"
                    : "No host Wi-Fi connection",
           1400);
}

static void wifi_create(lv_obj_t *parent) {
  lv_obj_t *window = wm_create_window(parent, "Wi-Fi Settings");
  bool connected = ask_windows_about_wifi();
  char status[80];
  snprintf(status, sizeof(status), "Host Wi-Fi: %s",
           connected ? connected_ssid : "Not connected or unavailable");
  lv_obj_t *state = wm_add_text(window, status);
  lv_obj_set_style_text_color(state,
                              connected ? lv_color_hex(0x52E3AC)
                                        : lv_color_hex(0xA4B1B6),
                              0);
  lv_obj_t *notice = wm_add_text(window,
      "Read-only host status. The simulator never changes your Wi-Fi connection.");
  lv_label_set_long_mode(notice, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(notice, 282);
  lv_obj_set_style_text_font(notice, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(notice, lv_color_hex(0x91ABB3), 0);
  lv_obj_t *refresh = wm_add_button(window, "Refresh host Wi-Fi", 230, 40);
  wm_on_tap(refresh, wifi_refresh, state);
}

static bool host_is_directory(const char *path) {
#ifdef _WIN32
  DWORD attributes = GetFileAttributesA(path);
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
  struct stat info;
  return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

static void fm_render(fm_context_t *fm);

static bool remove_tree(const char *path) {
#ifdef _WIN32
  if (!host_is_directory(path))
    return DeleteFileA(path) != 0;
  char query[PATH_LEN];
  snprintf(query, sizeof(query), "%s\\*", path);
  WIN32_FIND_DATAA entry;
  HANDLE handle = FindFirstFileA(query, &entry);
  if (handle != INVALID_HANDLE_VALUE) {
    do {
      if (strcmp(entry.cFileName, ".") == 0 || strcmp(entry.cFileName, "..") == 0)
        continue;
      char child[PATH_LEN];
      snprintf(child, sizeof(child), "%s\\%s", path, entry.cFileName);
      if (!remove_tree(child)) {
        FindClose(handle);
        return false;
      }
    } while (FindNextFileA(handle, &entry));
    FindClose(handle);
  }
  return RemoveDirectoryA(path) != 0;
#else
  if (!host_is_directory(path))
    return remove(path) == 0;
  DIR *directory = opendir(path);
  if (!directory)
    return false;
  struct dirent *entry;
  while ((entry = readdir(directory))) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    char child[PATH_LEN];
    snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
    if (!remove_tree(child)) {
      closedir(directory);
      return false;
    }
  }
  closedir(directory);
  return rmdir(path) == 0;
#endif
}

static void fm_action_cancel(lv_obj_t *object, void *user_data) {
  (void)object;
  fm_action_ctx_t *ctx = user_data;
  wm_keyboard_hide();
  lv_obj_del(ctx->modal);
  free(ctx);
}

static void fm_action_rename(lv_obj_t *object, void *user_data) {
  (void)object;
  fm_action_ctx_t *ctx = user_data;
  const char *new_name = lv_textarea_get_text(ctx->rename_input);
  if (!new_name[0] || strchr(new_name, '/') || strchr(new_name, '\\')) {
    wm_toast("Use a simple file name", 1400);
    return;
  }
  char parent[PATH_LEN], target[PATH_LEN];
  virtual_to_host(ctx->fm->virtual_path, parent, sizeof(parent));
  snprintf(target, sizeof(target), "%s/%s", parent, new_name);
#ifdef _WIN32
  bool renamed = MoveFileA(ctx->host_path, target) != 0;
#else
  bool renamed = rename(ctx->host_path, target) == 0;
#endif
  if (!renamed) {
    wm_toast("Rename failed", 1400);
    return;
  }
  wm_keyboard_hide();
  lv_obj_del(ctx->modal);
  fm_render(ctx->fm);
  free(ctx);
  wm_toast("Renamed", 1100);
}

static void fm_action_delete(lv_obj_t *object, void *user_data) {
  (void)object;
  fm_action_ctx_t *ctx = user_data;
  if (!remove_tree(ctx->host_path)) {
    wm_toast("Delete failed", 1400);
    return;
  }
  lv_obj_del(ctx->modal);
  fm_render(ctx->fm);
  free(ctx);
  wm_toast("Deleted", 1100);
}

static void fm_entry_hold(lv_obj_t *object, void *user_data) {
  (void)object;
  fm_entry_t *entry = user_data;
  if (!entry->fm || strcmp(entry->fm->virtual_path, "/") == 0 ||
      strcmp(entry->name, "..") == 0)
    return;
  fm_action_ctx_t *ctx = calloc(1, sizeof(*ctx));
  ctx->fm = entry->fm;
  char folder[PATH_LEN];
  virtual_to_host(entry->fm->virtual_path, folder, sizeof(folder));
  snprintf(ctx->host_path, sizeof(ctx->host_path), "%s/%s", folder, entry->name);
  ctx->modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(ctx->modal, 286, 206);
  lv_obj_center(ctx->modal);
  lv_obj_set_style_bg_color(ctx->modal, lv_color_hex(0x172126), 0);
  lv_obj_set_style_border_color(ctx->modal, lv_color_hex(0x25C8EF), 0);
  wm_add_text(ctx->modal, "Rename or delete item");
  ctx->rename_input = wm_add_input(ctx->modal, "New name", false);
  lv_textarea_set_text(ctx->rename_input, entry->name);
  lv_obj_align(ctx->rename_input, LV_ALIGN_CENTER, 0, -10);
  lv_obj_t *cancel = wm_add_button(ctx->modal, "Cancel", 80, 30);
  lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 10, -10);
  wm_on_tap(cancel, fm_action_cancel, ctx);
  lv_obj_t *rename_btn = wm_add_button(ctx->modal, "Rename", 80, 30);
  lv_obj_align(rename_btn, LV_ALIGN_BOTTOM_MID, 0, -10);
  wm_on_tap(rename_btn, fm_action_rename, ctx);
  lv_obj_t *delete_btn = wm_add_button(ctx->modal, "Delete", 80, 30);
  lv_obj_set_style_bg_color(delete_btn, lv_color_hex(0xE25162), 0);
  lv_obj_align(delete_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
  wm_on_tap(delete_btn, fm_action_delete, ctx);
}

static void editor_cancel(lv_obj_t *object, void *user_data) {
  (void)object;
  editor_ctx_t *editor = user_data;
  wm_keyboard_hide();
  lv_obj_del(editor->modal);
  free(editor);
}

static void editor_save(lv_obj_t *object, void *user_data) {
  (void)object;
  editor_ctx_t *editor = user_data;
  FILE *file = fopen(editor->host_path, "wb");
  if (!file) {
    wm_toast("Could not save file", 1400);
    return;
  }
  fputs(lv_textarea_get_text(editor->text_area), file);
  fclose(file);
  wm_keyboard_hide();
  lv_obj_del(editor->modal);
  free(editor);
  wm_toast("File saved", 1200);
}

static void package_install_cancel(lv_obj_t *object, void *user_data) {
  (void)object;
  package_install_ctx_t *ctx = user_data;
  lv_obj_del(ctx->modal);
  free(ctx);
}

static void package_install_confirm(lv_obj_t *object, void *user_data) {
  (void)object;
  package_install_ctx_t *ctx = user_data;
  char registry_path[PATH_LEN];
  snprintf(registry_path, sizeof(registry_path), "%s/int/apps/installed_apps.txt", SIM_ROOT);
  FILE *registry = fopen(registry_path, "wb");
  if (registry) {
    fputs(ctx->package_name, registry);
    fclose(registry);
  }
  lv_obj_del(ctx->modal);
  free(ctx);
  wm_reload_home();
  wm_toast("App installed in desktop registry", 1600);
}

static void fm_entry_tap(lv_obj_t *object, void *user_data) {
  (void)object;
  fm_entry_t *entry = user_data;
  fm_context_t *fm = entry->fm;
  if (strcmp(fm->virtual_path, "/") == 0) {
    snprintf(fm->virtual_path, sizeof(fm->virtual_path), "/%s", entry->name);
  } else if (strcmp(entry->name, "..") == 0) {
    char *slash = strrchr(fm->virtual_path, '/');
    if (slash && slash != fm->virtual_path)
      *slash = '\0';
    else
      strcpy(fm->virtual_path, "/");
  } else if (entry->is_dir) {
    size_t used = strlen(fm->virtual_path);
    snprintf(fm->virtual_path + used, sizeof(fm->virtual_path) - used, "/%s",
             entry->name);
  } else {
    char path[PATH_LEN];
    virtual_to_host(fm->virtual_path, path, sizeof(path));
    snprintf(path + strlen(path), sizeof(path) - strlen(path), "/%s", entry->name);
    const char *extension = strrchr(entry->name, '.');
    if (extension && strcmp(extension, ".vpk") == 0) {
      package_install_ctx_t *install_ctx = calloc(1, sizeof(*install_ctx));
      strncpy(install_ctx->package_name, entry->name,
              sizeof(install_ctx->package_name) - 1);
      lv_obj_t *modal = lv_obj_create(lv_layer_top());
      install_ctx->modal = modal;
      lv_obj_set_size(modal, 286, 176);
      lv_obj_center(modal);
      lv_obj_set_style_bg_color(modal, lv_color_hex(0x172126), 0);
      lv_obj_set_style_border_color(modal, lv_color_hex(0x25C8EF), 0);
      wm_add_text(modal, "VPK package detected");
      lv_obj_t *detail = wm_add_text(modal,
          "Install adds a desktop app stub. ESP ELF code stays device-only.");
      lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(detail, 250);
      lv_obj_align(detail, LV_ALIGN_CENTER, 0, -10);
      lv_obj_t *install = wm_add_button(modal, "Install", 104, 32);
      lv_obj_align(install, LV_ALIGN_BOTTOM_LEFT, 16, -10);
      wm_on_tap(install, package_install_confirm, install_ctx);
      lv_obj_t *cancel = wm_add_button(modal, "Cancel", 104, 32);
      lv_obj_align(cancel, LV_ALIGN_BOTTOM_RIGHT, -16, -10);
      wm_on_tap(cancel, package_install_cancel, install_ctx);
    } else {
      editor_ctx_t *editor = calloc(1, sizeof(*editor));
      strncpy(editor->host_path, path, sizeof(editor->host_path) - 1);
      editor->modal = lv_obj_create(lv_layer_top());
      lv_obj_set_size(editor->modal, 308, 394);
      lv_obj_center(editor->modal);
      lv_obj_set_style_bg_color(editor->modal, lv_color_hex(0x172126), 0);
      lv_obj_set_style_border_color(editor->modal, lv_color_hex(0x25C8EF), 0);
      wm_add_text(editor->modal, entry->name);
      editor->text_area = lv_textarea_create(editor->modal);
      lv_obj_set_size(editor->text_area, 280, 266);
      lv_obj_align(editor->text_area, LV_ALIGN_CENTER, 0, -12);
      FILE *file = fopen(editor->host_path, "rb");
      if (file) {
        char content[4096];
        size_t read = fread(content, 1, sizeof(content) - 1, file);
        content[read] = '\0';
        lv_textarea_set_text(editor->text_area, content);
        fclose(file);
      }
      lv_obj_add_event_cb(editor->text_area, input_focus, LV_EVENT_PRESSED, NULL);
      lv_obj_t *cancel = wm_add_button(editor->modal, "Cancel", 104, 32);
      lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 16, -12);
      wm_on_tap(cancel, editor_cancel, editor);
      lv_obj_t *save = wm_add_button(editor->modal, "Save", 104, 32);
      lv_obj_align(save, LV_ALIGN_BOTTOM_RIGHT, -16, -12);
      wm_on_tap(save, editor_save, editor);
    }
  }
  fm_render(fm);
}

static void fm_add_entry(fm_context_t *fm, const char *name, bool is_dir) {
  fm_entry_t *entry = calloc(1, sizeof(*entry));
  entry->fm = fm;
  entry->is_dir = is_dir;
  strncpy(entry->name, name, sizeof(entry->name) - 1);
  lv_obj_t *button = lv_list_add_btn(fm->list,
      is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE, name);
  wm_on_tap(button, fm_entry_tap, entry);
  wm_on_hold(button, fm_entry_hold, entry);
}

static void fm_render(fm_context_t *fm) {
  lv_label_set_text(fm->path_label, fm->virtual_path);
  lv_obj_clean(fm->list);
  if (strcmp(fm->virtual_path, "/") == 0) {
    fm_add_entry(fm, "int", true);
    fm_add_entry(fm, "sdcard", true);
    return;
  }
  fm_add_entry(fm, "..", true);
  char host_path[PATH_LEN];
  virtual_to_host(fm->virtual_path, host_path, sizeof(host_path));
#ifdef _WIN32
  char query[PATH_LEN];
  snprintf(query, sizeof(query), "%s\\*", host_path);
  WIN32_FIND_DATAA item;
  HANDLE handle = FindFirstFileA(query, &item);
  if (handle == INVALID_HANDLE_VALUE)
    return;
  do {
    if (strcmp(item.cFileName, ".") == 0 || strcmp(item.cFileName, "..") == 0)
      continue;
    fm_add_entry(fm, item.cFileName,
                 (item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
  } while (FindNextFileA(handle, &item));
  FindClose(handle);
#else
  DIR *directory = opendir(host_path);
  if (!directory)
    return;
  struct dirent *item;
  while ((item = readdir(directory))) {
    if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0)
      continue;
    char item_path[PATH_LEN];
    snprintf(item_path, sizeof(item_path), "%s/%s", host_path, item->d_name);
    fm_add_entry(fm, item->d_name, host_is_directory(item_path));
  }
  closedir(directory);
#endif
}

static void fm_create_confirm(lv_obj_t *object, void *user_data) {
  (void)object;
  fm_modal_ctx_t *ctx = user_data;
  const char *name = lv_textarea_get_text(ctx->input);
  if (!name[0] || strchr(name, '/') || strchr(name, '\\')) {
    wm_toast("Use a simple file name", 1400);
    return;
  }
  char folder[PATH_LEN], target[PATH_LEN];
  virtual_to_host(ctx->fm->virtual_path, folder, sizeof(folder));
  snprintf(target, sizeof(target), "%s/%s", folder, name);
  bool success = false;
  if (ctx->make_dir) {
#ifdef _WIN32
    success = _mkdir(target) == 0;
#else
    success = mkdir(target, 0775) == 0;
#endif
  } else {
    FILE *file = fopen(target, "wb");
    success = file != NULL;
    if (file)
      fclose(file);
  }
  if (success) {
    wm_keyboard_hide();
    lv_obj_del(ctx->modal);
    fm_render(ctx->fm);
    wm_toast(ctx->make_dir ? "Folder created" : "File created", 1200);
    free(ctx);
  } else {
    wm_toast("Could not create item", 1400);
  }
}

static void fm_create_cancel(lv_obj_t *object, void *user_data) {
  (void)object;
  fm_modal_ctx_t *ctx = user_data;
  wm_keyboard_hide();
  lv_obj_del(ctx->modal);
  free(ctx);
}

static void fm_prompt_create(fm_context_t *fm, bool make_dir) {
  if (!fm || strcmp(fm->virtual_path, "/") == 0) {
    wm_toast("Open int or sdcard first", 1300);
    return;
  }
  fm_modal_ctx_t *ctx = calloc(1, sizeof(*ctx));
  ctx->fm = fm;
  ctx->make_dir = make_dir;
  ctx->modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(ctx->modal, 278, 180);
  lv_obj_center(ctx->modal);
  lv_obj_set_style_bg_color(ctx->modal, lv_color_hex(0x172126), 0);
  lv_obj_set_style_border_color(ctx->modal, lv_color_hex(0x25C8EF), 0);
  wm_add_text(ctx->modal, make_dir ? "New folder name" : "New file name");
  ctx->input = wm_add_input(ctx->modal, make_dir ? "e.g. notes" : "e.g. todo.txt", false);
  lv_obj_align(ctx->input, LV_ALIGN_CENTER, 0, -5);
  lv_obj_t *cancel = wm_add_button(ctx->modal, "Cancel", 92, 32);
  lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 12, -10);
  wm_on_tap(cancel, fm_create_cancel, ctx);
  lv_obj_t *create = wm_add_button(ctx->modal, "Create", 92, 32);
  lv_obj_align(create, LV_ALIGN_BOTTOM_RIGHT, -12, -10);
  wm_on_tap(create, fm_create_confirm, ctx);
}

static void fm_add_folder(lv_obj_t *object, void *user_data) {
  (void)object;
  fm_prompt_create(user_data, true);
}

static void fm_add_file(lv_obj_t *object, void *user_data) {
  (void)object;
  fm_prompt_create(user_data, false);
}

static void files_create(lv_obj_t *parent) {
  fm_context_t *fm = calloc(1, sizeof(*fm));
  strcpy(fm->virtual_path, "/");
  lv_obj_t *window = wm_create_window(parent, "File Manager");
  lv_obj_set_user_data(window, fm);
  lv_obj_t *actions = lv_obj_create(window);
  lv_obj_remove_style_all(actions);
  lv_obj_set_size(actions, 300, 32);
  lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *folder = wm_add_button(actions, "+ Folder", 140, 28);
  wm_on_tap(folder, fm_add_folder, fm);
  lv_obj_t *file = wm_add_button(actions, "+ File", 140, 28);
  wm_on_tap(file, fm_add_file, fm);
  fm->path_label = wm_add_text(window, "/");
  lv_obj_set_style_text_color(fm->path_label, lv_color_hex(0x3ADAFB), 0);
  fm->list = wm_create_scroll_list(window, 300, 300);
  fm_render(fm);
}

static void files_destroy(lv_obj_t *parent) {
  free(lv_obj_get_user_data(parent));
}

static void simulate_update(lv_obj_t *object, void *user_data) {
  (void)object;
  (void)user_data;
  wm_toast("Update staged — reboot is intentionally not simulated", 2300);
}

static void package_test_action(lv_obj_t *object, void *user_data) {
  (void)object;
  (void)user_data;
  wm_toast("App action completed", 1300);
}

static void settings_create(lv_obj_t *parent) {
  lv_obj_t *window = wm_create_window(parent, "System Settings");
  wm_add_text(window, "VantageOS  •  desktop simulation");
  lv_obj_t *details = wm_add_text(window,
      "Storage: sim_data/int and sim_data/sdcard\n"
      "Wi-Fi: deterministic networks and saved local credentials\n"
      "OTA: visual flow only; no firmware, boot partition, or host changes");
  lv_label_set_long_mode(details, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(details, 286);
  lv_obj_set_style_text_color(details, lv_color_hex(0xA8BEC5), 0);
  lv_obj_t *update = wm_add_button(window, "Simulate available update", 252, 40);
  wm_on_tap(update, simulate_update, NULL);
}

static void package_create(lv_obj_t *parent) {
  lv_obj_t *window = wm_create_window(parent, "Installed VPK App");
  wm_add_text(window, "This is the desktop stand-in for a VPK-installed app.");
  lv_obj_t *body = wm_add_text(window,
      "The file-manager install flow, registry visibility, app window, Home, "
      "Back, Home, and Recents behavior are all exercised here.\n\n"
      "An ESP-target ELF is not run on a desktop: it must be rebuilt for the "
      "simulator ABI before executable code can be shared.");
  lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(body, 286);
  lv_obj_set_style_text_color(body, lv_color_hex(0xA8BEC5), 0);
  lv_obj_t *toast = wm_add_button(window, "Test app action", 200, 40);
  wm_on_tap(toast, package_test_action, NULL);
}
