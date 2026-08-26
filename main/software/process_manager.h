#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "esp_err.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#include "lvgl.h"

static uint32_t current_pid = 1;

typedef enum {
  APP_STOPPED, // Terminated
  APP_STARTED, // Running
  APP_CRASHED  // Crashed :(
} app_status_t;

typedef struct {
  uint32_t pid;
  char name[16];
  app_status_t state;
  TaskHandle_t task_handle;
  lv_obj_t *app_screen;
} app_control_block_t;

bool kernel_start_app(app_control_block_t *app);
void kernel_terminate_app(app_control_block_t *app);
uint32_t get_next_pid();