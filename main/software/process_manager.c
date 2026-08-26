#include "process_manager.h"
#include "freertos/idf_additions.h"
#include <stdint.h>

uint32_t get_next_pid() { return current_pid++; }

bool kernel_start_app(app_control_block_t *app) {
  if (app == NULL)
    return false;
  if (app->state == APP_STARTED)
    return false;

  app->state = APP_STARTED;
  return true;
}

void kernel_terminate_app(app_control_block_t *app) {
  if (app == NULL || app->state == APP_STOPPED)
    return;

  if (app->task_handle != NULL) {
    TaskHandle_t taskToDelete = app->task_handle;
    app->task_handle = NULL;
    vTaskDelete(app->task_handle);
  }

  if (app->app_screen != NULL) {
    lv_obj_del(app->app_screen);
    app->app_screen = NULL;
  }

  app->state = APP_STOPPED;
  free(app);
}