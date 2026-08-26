#include "apps/app_launcher.h"
#include "esp_log.h"
#include "software/sdk_bridge.h"
#include <esp_elf.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "software/process_manager.h"
#include "wm.h"

static const char *TAG = "APP_LAUNCHER";

typedef const vantage_app_desc_t *(*get_desc_fn_t)(void);

bool app_launcher_exec_elf(const char *elf_path) {
  if (!elf_path) {
    ESP_LOGE(TAG, "[LAUNCH] Failed: NULL elf_path provided");
    return false;
  }

  ESP_LOGI(TAG, "[LAUNCH] Starting ELF execution: %s", elf_path);

  FILE *f = fopen(elf_path, "rb");
  if (!f) {
    ESP_LOGE(TAG, "[LAUNCH] Failed to open ELF at %s", elf_path);
    wm_toast("Failed to open ELF file", 2000);
    return false;
  }

  fseek(f, 0, SEEK_END);
  size_t size = ftell(f);
  fseek(f, 0, SEEK_SET);

  uint8_t *temp_buf = (uint8_t *)malloc(size);
  if (!temp_buf) {
    ESP_LOGE(TAG, "[LAUNCH] Out of memory for read buffer (%d bytes)",
             (int)size);
    fclose(f);
    wm_toast("Out of Memory!", 2000);
    return false;
  }

  size_t read_bytes = fread(temp_buf, 1, size, f);
  fclose(f);

  if (read_bytes != size) {
    ESP_LOGE(TAG, "[LAUNCH] Short read: expected %d, got %d", (int)size,
             (int)read_bytes);
    free(temp_buf);
    return false;
  }

  // Allocate loader context on heap so it survives past launcher scope
  sdk_app_instance_t *inst =
      (sdk_app_instance_t *)calloc(1, sizeof(sdk_app_instance_t));
  if (!inst) {
    free(temp_buf);
    return false;
  }

  if (esp_elf_init(&inst->elf_handle) != 0) {
    ESP_LOGE(TAG, "[LAUNCH] esp_elf_init failed");
    wm_toast("ELF Init Failed", 2000);
    free(temp_buf);
    free(inst);
    return false;
  }

  if (esp_elf_relocate(&inst->elf_handle, temp_buf) != 0) {
    ESP_LOGE(TAG, "[LAUNCH] esp_elf_relocate failed");
    wm_toast("ELF Relocate Failed", 2000);
    esp_elf_deinit(&inst->elf_handle);
    free(temp_buf);
    free(inst);
    return false;
  }

  free(temp_buf); // Payload buffer relocated to segment RAM

  if (!inst->elf_handle.entry) {
    ESP_LOGE(TAG, "[LAUNCH] ELF entry point NULL");
    esp_elf_deinit(&inst->elf_handle);
    free(inst);
    return false;
  }

  get_desc_fn_t get_app_descriptor =
      (get_desc_fn_t)(uintptr_t)inst->elf_handle.entry;
  const vantage_app_desc_t *sdk_app = get_app_descriptor();

  if (!sdk_app) {
    ESP_LOGE(TAG, "[LAUNCH] get_app_descriptor() returned NULL");
    wm_toast("Invalid App Descriptor!", 2000);
    esp_elf_deinit(&inst->elf_handle);
    free(inst);
    return false;
  }

  app_descriptor_t *os_app = sdk_bridge_create_os_app(sdk_app, "/sdcard", inst);
  if (!os_app) {
    esp_elf_deinit(&inst->elf_handle);
    free(inst);
    return false;
  }
  app_control_block_t *app = malloc(sizeof(app_control_block_t));
  app->pid = get_next_pid();
  strncpy(app->name, sdk_app->name, 15);

  if (!kernel_start_app(app)) {
    return false;
    wm_toast("Failed to run app!", 2000);
  }

  wm_open_app(os_app, app);
  return true;
}