#include "sdk_bridge.h"

#include "vantage_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif


/* Thanks to Afterdark x Sweater Weather for
 * stopping me from snapping my laptop in half.
 * Ts killed like 60% of my braincells.  */

typedef struct {
  const vantage_app_desc_t *portable;
  app_descriptor_t desktop;
} sim_sdk_app_t;

typedef struct {
  vantage_click_cb_t callback;
  void *user_data;
} sim_sdk_button_ctx_t;

static sim_sdk_app_t **wrapped_apps;
static size_t wrapped_app_count;
static sim_sdk_app_t *active_app;

static bool create_directory(const char *path) {
  if (!path || !path[0])
    return false;
#ifdef _WIN32
  return _mkdir(path) == 0 || errno == EEXIST;
#else
  return mkdir(path, 0775) == 0 || errno == EEXIST;
#endif
}

static void sdk_button_tap(lv_obj_t *obj, void *user_data) {
  (void)obj;
  sim_sdk_button_ctx_t *context = user_data;
  if (context && context->callback)
    context->callback(context->user_data);
}

static void sdk_button_destroy(lv_event_t *event) {
  free(lv_event_get_user_data(event));
}

void *sdk_create_window(const char *title) {
  return wm_create_window(NULL, title);
}

void *sdk_add_text(void *parent, const char *text) {
  return wm_add_text((lv_obj_t *)parent, text);
}

void *sdk_add_button(void *parent, const char *label,
                     vantage_click_cb_t on_click_cb, void *user_data) {
  lv_obj_t *button = wm_add_button((lv_obj_t *)parent, label, 190, 40);
  if (on_click_cb) {
    sim_sdk_button_ctx_t *context = calloc(1, sizeof(*context));
    if (!context)
      return button;
    context->callback = on_click_cb;
    context->user_data = user_data;
    wm_on_tap(button, sdk_button_tap, context);
    lv_obj_add_event_cb(button, sdk_button_destroy, LV_EVENT_DELETE, context);
  }
  return button;
}

void sdk_show_toast(const char *message, uint32_t duration_ms) {
  wm_toast(message, duration_ms);
}

void sdk_get_data_path(const char *filename, char *out_path, size_t max_len) {
  if (!out_path || max_len == 0)
    return;
  out_path[0] = '\0';
  const char *id = (active_app && active_app->portable && active_app->portable->id)
                       ? active_app->portable->id
                       : "com.vantage.unknown";
  const char *root = wm_sim_data_root();
  const char *safe_filename = filename ? filename : "";
  size_t apps_length = strlen(root) + strlen("/int/apps") + 1;
  size_t app_length = apps_length + strlen(id) + 1;
  size_t data_length = app_length + strlen("data") + 1;
  size_t output_length = data_length + strlen(safe_filename) + 1;
  if (output_length > max_len) {
    sdk_show_toast("App data path does not fit output buffer", 1800);
    return;
  }

  /* Do the fussy length math first. Path truncation is a spectacularly dull
   * bug to debug after an app has already written the wrong file. */
  char *apps_path = malloc(apps_length);
  char *app_path = malloc(app_length);
  char *data_path = malloc(data_length);
  if (!apps_path || !app_path || !data_path) {
    free(apps_path);
    free(app_path);
    free(data_path);
    sdk_show_toast("Out of memory creating app data path", 1800);
    return;
  }
  snprintf(apps_path, apps_length, "%s/int/apps", root);
  snprintf(app_path, app_length, "%s/%s", apps_path, id);
  snprintf(data_path, data_length, "%s/data", app_path);
  if (!create_directory(apps_path) || !create_directory(app_path) ||
      !create_directory(data_path)) {
    sdk_show_toast("Could not create app data directory", 1800);
  } else {
    snprintf(out_path, max_len, "%s/%s", data_path, safe_filename);
  }
  free(apps_path);
  free(app_path);
  free(data_path);
}

static void portable_on_create(lv_obj_t *parent) {
  sim_sdk_app_t *app = lv_obj_get_user_data(parent);
  if (!app || !app->portable)
    return;
  active_app = app;
  if (app->portable->lifecycle.on_create)
    app->portable->lifecycle.on_create(parent);
}

static void portable_on_destroy(lv_obj_t *parent) {
  sim_sdk_app_t *app = lv_obj_get_user_data(parent);
  if (app && app->portable && app->portable->lifecycle.on_destroy)
    app->portable->lifecycle.on_destroy();
  if (active_app == app)
    active_app = NULL;
}

/*
 * Why?
 * Because I'm a fucking idiot and I didn't read the docs properly.
 * */
const app_descriptor_t *sim_sdk_wrap_app(const vantage_app_desc_t *portable) {
  if (!portable || !portable->id || !portable->name)
    return NULL;
  for (size_t index = 0; index < wrapped_app_count; ++index) {
    if (wrapped_apps[index]->portable == portable)
      return &wrapped_apps[index]->desktop;
  }
  /* A little malloc soup, on purpose: the pointer list may grow, but every
   * app wrapper needs a forever-stable address while its Home icon exists. */
  sim_sdk_app_t *app = calloc(1, sizeof(*app));
  sim_sdk_app_t **expanded = realloc(wrapped_apps,
                                     (wrapped_app_count + 1) * sizeof(*wrapped_apps));
  if (!app || !expanded) {
    free(app);
    return NULL;
  }
  wrapped_apps = expanded;
  app->portable = portable;
  app->desktop.id = portable->id;
  app->desktop.title = portable->name;
  app->desktop.on_create = portable_on_create;
  app->desktop.on_destroy = portable_on_destroy;
  app->desktop.payload = app;
  wrapped_apps[wrapped_app_count++] = app;
  return &app->desktop;
}

void sim_sdk_focus_app(const app_descriptor_t *desktop_app) {
  active_app = NULL;
  for (size_t index = 0; index < wrapped_app_count; ++index) {
    if (&wrapped_apps[index]->desktop == desktop_app) {
      active_app = wrapped_apps[index];
      return;
    }
  }
}
