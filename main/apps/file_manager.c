#include "apps/file_manager.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "drivers/storage.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "package/vpk_installer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wm.h"

static const char *TAG = "FILE_MANAGER";

typedef struct {
  char current_path[256];
  lv_obj_t *path_label;
  lv_obj_t *list_container;
} fm_context_t;

typedef struct {
  char file_path[280];
  lv_obj_t *ta;
  lv_obj_t *modal;
} fm_editor_ctx_t;

typedef struct {
  fm_context_t *fm_ctx;
  char target_path[280];
  char target_name[128];
  lv_obj_t *backdrop;
  lv_obj_t *ren_modal;
  lv_obj_t *rename_ta;
} fm_action_ctx_t;

typedef struct {
  char vpk_path[280];
  char target_base[32];
  lv_obj_t *modal;
} vpk_install_ctx_t;

static void *fm_read_file_psram(const char *path, size_t *out_size) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;

  fseek(f, 0, SEEK_END);
  size_t sz = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *psram_buf = (char *)heap_caps_malloc(sz + 1, MALLOC_CAP_SPIRAM);
  if (!psram_buf) {
    fclose(f);
    return NULL;
  }

  size_t read_bytes = fread(psram_buf, 1, sz, f);
  fclose(f);

  psram_buf[read_bytes] = '\0';
  if (out_size)
    *out_size = read_bytes;

  return psram_buf;
}

static void render_directory(fm_context_t *ctx);

static void vpk_cancel_cb(lv_obj_t *btn, void *user_data) {
  (void)btn;
  vpk_install_ctx_t *ictx = (vpk_install_ctx_t *)user_data;
  lv_obj_del(ictx->modal);
  free(ictx);
}

static void vpk_install_finished_cb(void *user_data) {
  vpk_err_t err = (vpk_err_t)(uintptr_t)user_data;

  if (err == VPK_OK) {
    wm_toast("App Installed Successfully!", 2000);
    wm_reload_home();
  } else {
    wm_toast(vpk_err_to_str(err), 3000);
  }
}

// Clean worker task with zero LVGL calls to prevent thread crashes

static void vpk_install_task(void *pvParameters) {
  vpk_install_ctx_t *ictx = (vpk_install_ctx_t *)pvParameters;

  ESP_LOGI(TAG, "Starting VPK installation task...");
  esp_err_t err = vpk_install_package(ictx->vpk_path, ictx->target_base);
  ESP_LOGI(TAG, "VPK installation completed with code: 0x%x", err);

  wm_lvgl_lock();
  lv_async_call(vpk_install_finished_cb, (void *)(uintptr_t)err);
  wm_lvgl_unlock();

  free(ictx);
  vTaskDelete(NULL);
}

static void vpk_confirm_install_cb(lv_obj_t *btn, void *user_data) {
  (void)btn;
  vpk_install_ctx_t *ictx = (vpk_install_ctx_t *)user_data;

  // Delete modal immediately on UI thread BEFORE launching worker
  if (ictx->modal && lv_obj_is_valid(ictx->modal)) {
    lv_obj_del(ictx->modal);
    ictx->modal = NULL;
  }

  // Spawn worker task with 8 KB stack (plenty since buffers use heap)
  BaseType_t ret =
      xTaskCreate(vpk_install_task, "vpk_worker", 8192, ictx, 5, NULL);

  if (ret != pdPASS) {
    wm_toast("Task creation failed! (Low SRAM)", 2000);
    free(ictx);
    return;
  }

  wm_toast("Installing package...", 1500);
  ESP_LOGI(TAG, "Installing...");
}

static void open_vpk_installer_modal(const char *path,
                                     const char *current_dir) {
  vpk_manifest_t *manifest = (vpk_manifest_t *)malloc(sizeof(vpk_manifest_t));
  if (!manifest) {
    wm_toast("Out of memory!", 2000);
    return;
  }

  if (vpk_inspect_package(path, manifest) != ESP_OK) {
    wm_toast("Invalid VPK Archive!", 2000);
    free(manifest);
    return;
  }

  lv_obj_t *modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(modal, 290, 220);
  lv_obj_center(modal);
  lv_obj_set_style_bg_color(modal, lv_color_hex(0x121212), 0);
  lv_obj_set_style_border_color(modal, lv_color_hex(0x00E5FF), 0);
  lv_obj_set_style_pad_all(modal, 10, 0);

  char title_buf[128];
  snprintf(title_buf, sizeof(title_buf), "Install %s?", manifest->name);
  wm_add_text(modal, title_buf);

  char meta_buf[128];
  snprintf(meta_buf, sizeof(meta_buf), "ID: %s\nVersion: %s", manifest->id,
           manifest->version);
  lv_obj_t *lbl_meta = wm_add_text(modal, meta_buf);
  lv_obj_set_style_text_color(lbl_meta, lv_color_hex(0xAAAAAA), 0);

  // Free the stack-heavy manifest once labels are created
  free(manifest);

  vpk_install_ctx_t *ictx =
      (vpk_install_ctx_t *)malloc(sizeof(vpk_install_ctx_t));
  strncpy(ictx->vpk_path, path, sizeof(ictx->vpk_path));

  // Extract drive root mount prefix (/int or /sdcard)
  if (strncmp(current_dir, "/int", 4) == 0) {
    strcpy(ictx->target_base, "/int");
  } else {
    strcpy(ictx->target_base, "/sdcard");
  }
  ictx->modal = modal;

  // Bottom Action Bar
  lv_obj_t *btn_bar = lv_obj_create(modal);
  lv_obj_remove_style_all(btn_bar);
  lv_obj_set_size(btn_bar, 270, 36);
  lv_obj_align(btn_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_flex_flow(btn_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn_bar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *btn_canc = wm_add_button(btn_bar, "Cancel", 125, 32);
  wm_on_tap(btn_canc, vpk_cancel_cb, ictx);

  lv_obj_t *btn_inst = wm_add_button(btn_bar, "Install", 125, 32);
  wm_on_tap(btn_inst, vpk_confirm_install_cb, ictx);
}

static void editor_save_cb(lv_obj_t *btn, void *user_data) {
  (void)btn;
  fm_editor_ctx_t *ed = (fm_editor_ctx_t *)user_data;
  const char *text = lv_textarea_get_text(ed->ta);
  size_t len = strlen(text);

  if (storage_psram_flush_write(ed->file_path, text, len) == ESP_OK) {
    wm_toast("File Saved!", 1500);
  } else {
    wm_toast("Save Failed!", 2000);
  }

  wm_keyboard_hide();
  lv_obj_del(ed->modal);
  free(ed);
}

static void editor_cancel_cb(lv_obj_t *btn, void *user_data) {
  (void)btn;
  fm_editor_ctx_t *ed = (fm_editor_ctx_t *)user_data;
  wm_keyboard_hide();
  lv_obj_del(ed->modal);
  free(ed);
}

static void editor_ta_gesture_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  // Trigger keyboard ONLY on double tap
  if (code == LV_EVENT_DOUBLE_CLICKED) {
    wm_keyboard_attach(lv_event_get_target(e));
  }
}

static void open_file_editor(const char *path) {
  size_t file_sz = 0;
  char *content = (char *)fm_read_file_psram(path, &file_sz);

  lv_obj_t *modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(modal, 310, 400);
  lv_obj_center(modal);
  lv_obj_set_flex_flow(modal, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(modal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_color(modal, lv_color_hex(0x121212), 0);
  lv_obj_set_style_border_color(modal, lv_color_hex(0x00E5FF), 0);
  lv_obj_set_style_pad_all(modal, 6, 0);

  lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

  const char *filename = strrchr(path, '/');
  wm_add_text(modal, filename ? filename + 1 : path);

  lv_obj_t *ta = lv_textarea_create(modal);
  lv_obj_set_size(ta, 290, 290);
  lv_textarea_set_placeholder_text(ta, "Empty file...");
  lv_obj_clear_flag(ta,
                    LV_OBJ_FLAG_SCROLL_ON_FOCUS); // Disable auto-scroll jump

  if (content) {
    lv_textarea_set_text(ta, content);
    heap_caps_free(content);
  }

  // Register double-click listener instead of single press
  lv_obj_add_event_cb(ta, editor_ta_gesture_cb, LV_EVENT_DOUBLE_CLICKED, NULL);

  fm_editor_ctx_t *ed = (fm_editor_ctx_t *)malloc(sizeof(fm_editor_ctx_t));
  strncpy(ed->file_path, path, sizeof(ed->file_path));
  ed->ta = ta;
  ed->modal = modal;

  // Bottom Button Bar Container
  lv_obj_t *btn_bar = lv_obj_create(modal);
  lv_obj_remove_style_all(btn_bar);
  lv_obj_set_size(btn_bar, 290, 36);
  lv_obj_align(btn_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_flex_flow(btn_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn_bar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *btn_canc = wm_add_button(btn_bar, "Cancel", 135, 32);
  wm_on_tap(btn_canc, editor_cancel_cb, ed);

  lv_obj_t *btn_save = wm_add_button(btn_bar, "Save", 135, 32);
  wm_on_tap(btn_save, editor_save_cb, ed);
}

// FILE CONTEXT / ACTION MENU (HOLD)

static void backdrop_click_dismiss_cb(lv_obj_t *obj, void *user_data) {
  (void)obj;
  fm_action_ctx_t *act = (fm_action_ctx_t *)user_data;
  wm_keyboard_hide();

  // Delete child rename modal first if it exists
  if (act->ren_modal && lv_obj_is_valid(act->ren_modal)) {
    lv_obj_del(act->ren_modal);
  }

  if (act->backdrop && lv_obj_is_valid(act->backdrop)) {
    lv_obj_del(act->backdrop);
  }
  free(act);
}

static void context_menu_panel_click_cb(lv_event_t *e) {
  lv_event_stop_processing(e);
}

static void action_delete_confirm_cb(lv_obj_t *btn, void *user_data) {
  (void)btn;
  fm_action_ctx_t *act = (fm_action_ctx_t *)user_data;
  if (storage_remove_recursive(act->target_path) == ESP_OK) {
    wm_toast("Deleted successfully!", 1500);
  } else {
    wm_toast("Delete failed!", 2000);
  }

  render_directory(act->fm_ctx);
  backdrop_click_dismiss_cb(NULL, act);
}

static void action_rename_confirm_cb(lv_obj_t *btn, void *user_data) {
  (void)btn;
  fm_action_ctx_t *act = (fm_action_ctx_t *)user_data;
  const char *new_name = lv_textarea_get_text(act->rename_ta);

  if (strlen(new_name) == 0) {
    wm_toast("Invalid Name!", 1500);
    return;
  }

  char new_path[300];
  snprintf(new_path, sizeof(new_path), "%s/%s", act->fm_ctx->current_path,
           new_name);

  if (rename(act->target_path, new_path) == 0) {
    wm_toast("Renamed!", 1500);
  } else {
    wm_toast("Rename failed!", 2000);
  }

  wm_keyboard_hide();
  render_directory(act->fm_ctx);
  backdrop_click_dismiss_cb(NULL, act);
}

static void show_rename_modal(lv_obj_t *obj, void *user_data) {
  (void)obj;
  fm_action_ctx_t *act = (fm_action_ctx_t *)user_data;

  lv_obj_t *ren_modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(ren_modal, 280, 180);
  lv_obj_center(ren_modal);
  lv_obj_set_style_bg_color(ren_modal, lv_color_hex(0x1a1a1a), 0);
  lv_obj_set_style_border_color(ren_modal, lv_color_hex(0x00E5FF), 0);

  wm_add_text(ren_modal, "Rename item to:");
  act->rename_ta = wm_add_input(ren_modal, act->target_name, false);
  lv_obj_align(act->rename_ta, LV_ALIGN_CENTER, 0, -10);

  lv_obj_t *btn_canc = wm_add_button(ren_modal, "Cancel", 90, 32);
  lv_obj_align(btn_canc, LV_ALIGN_BOTTOM_LEFT, 10, -5);
  wm_on_tap(btn_canc, backdrop_click_dismiss_cb, act);

  lv_obj_t *btn_ren = wm_add_button(ren_modal, "Save", 90, 32);
  lv_obj_align(btn_ren, LV_ALIGN_BOTTOM_RIGHT, -10, -5);
  wm_on_tap(btn_ren, action_rename_confirm_cb, act);
}

static void show_file_context_menu(fm_context_t *ctx, const char *entry_name) {
  lv_obj_t *backdrop = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(backdrop);
  lv_obj_set_size(backdrop, 320, 480);
  lv_obj_add_flag(backdrop, LV_OBJ_FLAG_CLICKABLE);

  fm_action_ctx_t *act = (fm_action_ctx_t *)malloc(sizeof(fm_action_ctx_t));
  act->fm_ctx = ctx;
  strncpy(act->target_name, entry_name, sizeof(act->target_name));
  snprintf(act->target_path, sizeof(act->target_path), "%s/%s",
           ctx->current_path, entry_name);
  act->backdrop = backdrop;

  wm_on_tap(backdrop, backdrop_click_dismiss_cb, act);

  lv_obj_t *panel = lv_obj_create(backdrop);
  lv_obj_set_size(panel, 220, 150);
  lv_obj_center(panel);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x1e1e1e), 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(0x00E5FF), 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_add_event_cb(panel, context_menu_panel_click_cb, LV_EVENT_CLICKED,
                      NULL);

  wm_add_text(panel, entry_name);

  lv_obj_t *btn_rename = wm_add_button(panel, "Rename", 180, 34);
  lv_obj_align(btn_rename, LV_ALIGN_CENTER, 0, -10);
  wm_on_tap(btn_rename, show_rename_modal, act);

  lv_obj_t *btn_delete = wm_add_button(panel, "Delete", 180, 34);
  lv_obj_align(btn_delete, LV_ALIGN_CENTER, 0, 30);
  lv_obj_set_style_bg_color(btn_delete, lv_palette_main(LV_PALETTE_RED), 0);
  wm_on_tap(btn_delete, action_delete_confirm_cb, act);
}

typedef struct {
  fm_context_t *fm_ctx;
  lv_obj_t *input_ta;
  lv_obj_t *modal;
  bool is_folder;
} create_item_ctx_t;

static void create_confirm_cb(lv_obj_t *btn, void *user_data) {
  (void)btn;
  create_item_ctx_t *cctx = (create_item_ctx_t *)user_data;
  const char *name = lv_textarea_get_text(cctx->input_ta);

  if (strlen(name) == 0) {
    wm_toast("Name cannot be empty!", 1500);
    return;
  }

  char target_path[280];
  snprintf(target_path, sizeof(target_path), "%s/%s",
           cctx->fm_ctx->current_path, name);

  if (cctx->is_folder) {
    if (mkdir(target_path, 0775) == 0) {
      wm_toast("Folder Created", 1500);
    } else {
      wm_toast("Folder Creation Failed", 2000);
    }
  } else {
    if (storage_psram_flush_write(target_path, "", 0) == ESP_OK) {
      wm_toast("File Created", 1500);
    } else {
      wm_toast("File Creation Failed", 2000);
    }
  }

  wm_keyboard_hide();
  lv_obj_del(cctx->modal);
  render_directory(cctx->fm_ctx);
  free(cctx);
}

static void create_cancel_cb(lv_obj_t *btn, void *user_data) {
  (void)btn;
  create_item_ctx_t *cctx = (create_item_ctx_t *)user_data;
  wm_keyboard_hide();
  lv_obj_del(cctx->modal);
  free(cctx);
}

static void prompt_create_item(fm_context_t *ctx, bool is_folder) {
  if (strcmp(ctx->current_path, "/") == 0) {
    wm_toast("Select drive first!", 1500);
    return;
  }

  lv_obj_t *modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(modal, 280, 180);
  lv_obj_center(modal);
  lv_obj_set_style_bg_color(modal, lv_color_hex(0x1a1a1a), 0);
  lv_obj_set_style_border_color(modal, lv_color_hex(0x00E5FF), 0);

  wm_add_text(modal, is_folder ? "New Folder Name:" : "New File Name:");

  lv_obj_t *ta =
      wm_add_input(modal, is_folder ? "e.g. docs" : "e.g. test.txt", false);
  lv_obj_align(ta, LV_ALIGN_CENTER, 0, -10);

  create_item_ctx_t *cctx =
      (create_item_ctx_t *)malloc(sizeof(create_item_ctx_t));
  cctx->fm_ctx = ctx;
  cctx->input_ta = ta;
  cctx->modal = modal;
  cctx->is_folder = is_folder;

  lv_obj_t *btn_canc = wm_add_button(modal, "Cancel", 90, 32);
  lv_obj_align(btn_canc, LV_ALIGN_BOTTOM_LEFT, 10, -5);
  wm_on_tap(btn_canc, create_cancel_cb, cctx);

  lv_obj_t *btn_ok = wm_add_button(modal, "Create", 90, 32);
  lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_RIGHT, -10, -5);
  wm_on_tap(btn_ok, create_confirm_cb, cctx);
}

static void add_folder_btn_tap_cb(lv_obj_t *obj, void *user_data) {
  (void)obj;
  prompt_create_item((fm_context_t *)user_data, true);
}

static void add_file_btn_tap_cb(lv_obj_t *obj, void *user_data) {
  (void)obj;
  prompt_create_item((fm_context_t *)user_data, false);
}

static void file_item_hold_cb(lv_obj_t *obj, void *user_data) {
  fm_context_t *ctx = (fm_context_t *)user_data;
  const char *entry_name = lv_list_get_btn_text(ctx->list_container, obj);

  if (!entry_name || strcmp(entry_name, "..") == 0 ||
      strcmp(ctx->current_path, "/") == 0) {
    return;
  }
  show_file_context_menu(ctx, entry_name);
}

static void file_item_tap_cb(lv_obj_t *obj, void *user_data) {
  fm_context_t *ctx = (fm_context_t *)user_data;
  const char *entry_name = lv_list_get_btn_text(ctx->list_container, obj);

  if (!entry_name)
    return;

  if (strcmp(ctx->current_path, "/") == 0) {
    snprintf(ctx->current_path, sizeof(ctx->current_path), "/%s", entry_name);
    render_directory(ctx);
    return;
  }

  if (strcmp(entry_name, "..") == 0) {
    char *last_slash = strrchr(ctx->current_path, '/');
    if (last_slash && last_slash != ctx->current_path) {
      *last_slash = '\0';
    } else {
      strcpy(ctx->current_path, "/");
    }
    render_directory(ctx);
    return;
  }

  char target_path[280];
  snprintf(target_path, sizeof(target_path), "%s/%s", ctx->current_path,
           entry_name);

  struct stat st;
  if (stat(target_path, &st) == 0) {
    if (S_ISDIR(st.st_mode)) {
      strncpy(ctx->current_path, target_path, sizeof(ctx->current_path));
      render_directory(ctx);
    } else {
      // Intercept .vpk package tap vs standard text/data file
      if (strstr(entry_name, ".vpk") != NULL) {
        open_vpk_installer_modal(target_path, ctx->current_path);
      } else {
        open_file_editor(target_path);
      }
    }
  }
}

static void render_directory(fm_context_t *ctx) {
  lv_label_set_text(ctx->path_label, ctx->current_path);
  lv_obj_clean(ctx->list_container);

  if (strcmp(ctx->current_path, "/") == 0) {
    lv_obj_t *b1 = lv_list_add_btn(ctx->list_container, LV_SYMBOL_DRIVE, "int");
    wm_on_tap(b1, file_item_tap_cb, ctx);

    lv_obj_t *b2 =
        lv_list_add_btn(ctx->list_container, LV_SYMBOL_SD_CARD, "sdcard");
    wm_on_tap(b2, file_item_tap_cb, ctx);
    return;
  }

  lv_obj_t *back_btn =
      lv_list_add_btn(ctx->list_container, LV_SYMBOL_LEFT, "..");
  wm_on_tap(back_btn, file_item_tap_cb, ctx);

  DIR *dir = opendir(ctx->current_path);
  if (!dir) {
    wm_toast("Cannot open directory", 2000);
    return;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    char full_path[300];
    snprintf(full_path, sizeof(full_path), "%s/%s", ctx->current_path,
             entry->d_name);

    struct stat st;
    bool is_dir = false;
    if (stat(full_path, &st) == 0) {
      is_dir = S_ISDIR(st.st_mode);
    }

    const char *symbol = is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE;
    lv_obj_t *btn = lv_list_add_btn(ctx->list_container, symbol, entry->d_name);

    wm_on_tap(btn, file_item_tap_cb, ctx);
    wm_on_hold(btn, file_item_hold_cb, ctx);
  }
  closedir(dir);
}

// APP LIFECYCLE
static void fm_create_cb(lv_obj_t *parent) {
  fm_context_t *ctx = (fm_context_t *)calloc(1, sizeof(fm_context_t));
  strcpy(ctx->current_path, "/");

  lv_obj_t *win = wm_create_window(parent, "File Manager");

  lv_obj_t *action_bar = lv_obj_create(win);
  lv_obj_remove_style_all(action_bar);
  lv_obj_set_size(action_bar, 300, 32);
  lv_obj_set_flex_flow(action_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(action_bar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *btn_add_dir = wm_add_button(action_bar, "+ Folder", 140, 28);
  wm_on_tap(btn_add_dir, add_folder_btn_tap_cb, ctx);

  lv_obj_t *btn_add_file = wm_add_button(action_bar, "+ File", 140, 28);
  wm_on_tap(btn_add_file, add_file_btn_tap_cb, ctx);

  ctx->path_label = wm_add_text(win, "/");
  lv_obj_set_style_text_color(ctx->path_label, lv_color_hex(0x00E5FF), 0);

  ctx->list_container = wm_create_scroll_list(win, 300, 300);
  lv_obj_set_user_data(win, ctx);

  render_directory(ctx);
}

static void fm_destroy_cb(lv_obj_t *parent) {
  fm_context_t *ctx = (fm_context_t *)lv_obj_get_user_data(parent);
  if (ctx)
    free(ctx);
}

const app_descriptor_t file_manager_app = {.id = "com.vantage.filemanager",
                                           .title = "File Manager",
                                           .on_create = fm_create_cb,
                                           .on_destroy = fm_destroy_cb};