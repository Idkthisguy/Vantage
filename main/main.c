#include "drivers/sd_card.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

#include "display.h"
#include "lvgl.h"

#include "drivers/wifi.h"
#include "wm.h"

#include "esp_ota_ops.h"

#include "apps/file_manager.h"
#include "drivers/storage.h"

#include "data/panic.h"

#include "nvs_flash.h"

static const char *TAG = "VANTAGE_OS";

TaskHandle_t kernel_task_handle = NULL;
TaskHandle_t ui_task_handle = NULL;

static void anim_set_width_cb(void *var, int32_t v) {
  lv_obj_set_width((lv_obj_t *)var, (lv_coord_t)v);
}

static void anim_set_height_cb(void *var, int32_t v) {
  lv_obj_set_height((lv_obj_t *)var, (lv_coord_t)v);
}

static void anim_set_border_opa_cb(void *var, int32_t v) {
  lv_obj_set_style_border_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

// Commented out just in case for touch debugging

/* static void touch_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_PRESSING) {
        lv_indev_t * indev = lv_indev_get_act();
        lv_point_t point;
        lv_indev_get_point(indev, &point);

        lv_obj_t * dot = lv_obj_create(lv_scr_act());
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_pos(dot, point.x - 4, point.y - 4);

        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_palette_main(LV_PALETTE_CYAN), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    }
}*/

static void create_demo_ui(void) { // Basically nothing but keep just in case ;)
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x121212), 0);

  // Label
  lv_obj_t *label = lv_label_create(scr);
  lv_label_set_text(label, "VANTAGE OS");
  lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, -40);

  // Spinner (LVGL v9 syntax)
  lv_obj_t *spinner = lv_spinner_create(scr);
  lv_spinner_set_anim_params(spinner, 1000,
                             60); // Set time (ms) and sweep angle
  lv_obj_set_size(spinner, 60, 60);
  lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 30);

  // Register full-screen touch listener
  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
}

void kernel_service_task(void *pvParameters) {
  ESP_LOGI(TAG, "Kernel Task running on Core %d", xPortGetCoreID());

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

static void lvgl_tick_cb(void *arg) { lv_tick_inc(1); }

void init_lvgl_tick_timer(void) {
  const esp_timer_create_args_t lvgl_tick_timer_args = {
      .callback = &lvgl_tick_cb, .name = "lvgl_tick"};

  esp_timer_handle_t lvgl_tick_timer;
  ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, 1000));
}

void ui_render_task(void *pvParameters) {
  display_lvgl_init();
  wm_init();
  init_lvgl_tick_timer();

  wm_lvgl_lock();
  wm_lvgl_unlock();

  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xPeriod = 10;

  while (1) {
    wm_lvgl_lock();
    lv_timer_handler();
    wm_check_update_ui_state();

    wm_lvgl_unlock();

    vTaskDelayUntil(&xLastWakeTime, xPeriod);
  }
}

void app_main(void) {
  // Separates and indicate when Vantage boots
  ESP_LOGI(TAG, "====================================");
  ESP_LOGI(TAG, "       VANTAGE OS BOOTING           ");
  ESP_LOGI(TAG, "====================================");

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  display_init();

  esp_ota_mark_app_valid_cancel_rollback();

  if (dual_storage_init() == ESP_OK) {
    ESP_LOGI(TAG, "Primary Storage (/int) Ready.");
  } else {
    vantage_kernel_panic(PANIC_STORAGE_FAILURE,
                         "PRIMARY INT FLASH MOUNT FAILED");
  }

  // SD Card failure is non-fatal (system falls back to single-storage)
  if (fs_init() == ESP_OK) {
    fs_create_default_dirs();
    ESP_LOGI(TAG, "Secondary SD Storage (/sdcard) Ready.");
  } else {
    ESP_LOGW(TAG, "SD Card not mounted. Running in single-storage mode.");
  }

  wifi_manager_init();
  wifi_auto_connect_saved();

  BaseType_t res1 = xTaskCreatePinnedToCore(
      kernel_service_task, "KernelTask", 4096, NULL, 5, &kernel_task_handle, 0);

  BaseType_t res2 = xTaskCreatePinnedToCore(ui_render_task, "UITask", 16384,
                                            NULL, 10, &ui_task_handle, 1);

  if (res1 != pdPASS || res2 != pdPASS) {
    vantage_kernel_panic(PANIC_OUT_OF_MEMORY,
                         "FAILED TO ALLOCATE SYSTEM TASKS");
  }
}