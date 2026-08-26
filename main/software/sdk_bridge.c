#include "software/sdk_bridge.h"
#include "drivers/storage.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "SDK_BRIDGE";
static char current_active_mount[32] = "/sdcard";
static char current_active_app_id[64] = "com.vantage.system";

static const sdk_app_desc_t *current_sdk_app = NULL;
static void *current_payload = NULL;

void sdk_bridge_set_active_app(const sdk_app_desc_t *app_desc,
                               const char *mount_point, void *payload) {
  if (mount_point) {
    strncpy(current_active_mount, mount_point,
            sizeof(current_active_mount) - 1);
  }
  if (app_desc && app_desc->id) {
    strncpy(current_active_app_id, app_desc->id,
            sizeof(current_active_app_id) - 1);
    current_sdk_app = app_desc;
  }
  current_payload = payload;
}

void sdk_get_data_path(const char *filename, char *out_path, size_t max_len) {
  snprintf(out_path, max_len, "%s/apps/%s/data/%s", current_active_mount,
           current_active_app_id, filename ? filename : "");
}

void *sdk_create_window(const char *title) {
  return (void *)wm_create_window(NULL, title);
}

typedef struct {
  void (*cb)(void *);
  void *user_data;
} sdk_btn_ctx_t;

static void internal_btn_tap_adapter(lv_obj_t *obj, void *user_data) {
  sdk_btn_ctx_t *ctx = (sdk_btn_ctx_t *)user_data;
  if (ctx && ctx->cb) {
    ctx->cb(ctx->user_data);
  }
}

static void btn_delete_cb(lv_event_t *e) {
  void *ctx = lv_event_get_user_data(e);
  if (ctx)
    free(ctx);
}

void *sdk_add_button(void *parent, const char *label,
                     void (*on_click_cb)(void *user_data), void *user_data) {
  lv_obj_t *btn = wm_add_button((lv_obj_t *)parent, label, 180, 40);
  if (on_click_cb) {
    sdk_btn_ctx_t *ctx = (sdk_btn_ctx_t *)malloc(sizeof(sdk_btn_ctx_t));
    ctx->cb = on_click_cb;
    ctx->user_data = user_data;

    wm_on_tap(btn, internal_btn_tap_adapter, ctx);
    lv_obj_add_event_cb(btn, btn_delete_cb, LV_EVENT_DELETE, ctx);
  }
  return (void *)btn;
}

void *sdk_add_text(void *parent, const char *text) {
  return (void *)wm_add_text((lv_obj_t *)parent, text);
}

void sdk_show_toast(const char *message, uint32_t duration_ms) {
  wm_toast(message, duration_ms);
}

static void sdk_adapter_on_create(lv_obj_t *parent_container) {
  ESP_LOGI(
      TAG,
      "[GUI_DBG] sdk_adapter_on_create triggered. Parent container ptr: %p",
      parent_container);

  sdk_app_instance_t *inst =
      (sdk_app_instance_t *)lv_obj_get_user_data(parent_container);
  if (!inst) {
    ESP_LOGE(TAG, "[GUI_DBG] FAILED: user_data on parent_container %p is NULL!",
             parent_container);
    return;
  }

  if (!inst->sdk_app) {
    ESP_LOGE(TAG, "[GUI_DBG] FAILED: inst->sdk_app is NULL!");
    return;
  }

  ESP_LOGI(TAG, "[GUI_DBG] App ID: %s | App Name: %s",
           inst->sdk_app->id ? inst->sdk_app->id : "UNKNOWN",
           inst->sdk_app->name ? inst->sdk_app->name : "UNKNOWN");

  if (inst->sdk_app->lifecycle.on_create) {
    ESP_LOGI(
        TAG,
        "[GUI_DBG] Executing user on_create callback at target address %p...",
        inst->sdk_app->lifecycle.on_create);
    inst->sdk_app->lifecycle.on_create((void *)parent_container);
    ESP_LOGI(TAG, "[GUI_DBG] User on_create execution completed successfully.");
  } else {
    ESP_LOGW(TAG,
             "[GUI_DBG] WARNING: App lifecycle.on_create callback is NULL!");
  }
}

static void sdk_adapter_on_destroy(lv_obj_t *parent_container) {
  sdk_app_instance_t *inst =
      (sdk_app_instance_t *)lv_obj_get_user_data(parent_container);
  if (!inst)
    return;

  if (inst->sdk_app && inst->sdk_app->lifecycle.on_destroy) {
    inst->sdk_app->lifecycle.on_destroy(); // Call without arguments
  }

  esp_elf_deinit(&inst->elf_handle);
}

app_descriptor_t *sdk_bridge_create_os_app(const sdk_app_desc_t *sdk_app,
                                           const char *mount_point,
                                           void *payload) {
  if (!sdk_app)
    return NULL;

  sdk_app_instance_t *inst =
      (sdk_app_instance_t *)calloc(1, sizeof(sdk_app_instance_t));
  if (!inst)
    return NULL;

  // Deep copy the descriptor so heap lifetime is explicit
  sdk_app_desc_t *app_copy = (sdk_app_desc_t *)malloc(sizeof(sdk_app_desc_t));
  if (!app_copy) {
    free(inst);
    return NULL;
  }
  memcpy(app_copy, sdk_app, sizeof(sdk_app_desc_t));

  inst->sdk_app = app_copy;
  inst->payload = payload;

  sdk_bridge_set_active_app(app_copy, mount_point, payload);

  app_descriptor_t *os_app =
      (app_descriptor_t *)calloc(1, sizeof(app_descriptor_t));
  if (!os_app) {
    free(app_copy);
    free(inst);
    return NULL;
  }

  os_app->id = strdup(sdk_app->id ? sdk_app->id : "com.vantage.unknown");
  os_app->title = strdup(sdk_app->name ? sdk_app->name : "Unknown App");
  os_app->on_create = sdk_adapter_on_create;
  os_app->on_destroy = sdk_adapter_on_destroy;
  os_app->payload = inst;

  return os_app;
}

app_descriptor_t sdk_bridge_register_app(const sdk_app_desc_t *sdk_app,
                                         const char *mount_point) {
  app_descriptor_t os_app = {0};
  if (!sdk_app)
    return os_app;

  sdk_bridge_set_active_app(sdk_app, mount_point, NULL);

  os_app.id = sdk_app->id;
  os_app.title = sdk_app->name;
  os_app.on_create = sdk_adapter_on_create;
  os_app.on_destroy = sdk_adapter_on_destroy;
  os_app.payload = NULL;

  return os_app;
}