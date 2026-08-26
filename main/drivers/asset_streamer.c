#include "asset_streamer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdio.h>


static const char *TAG = "STREAMER";

void *asset_load_to_psram(const char *file_path, size_t *out_size) {
  FILE *f = fopen(file_path, "rb");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open file: %s", file_path);
    return NULL;
  }

  fseek(f, 0, SEEK_END);
  size_t sz = ftell(f);
  fseek(f, 0, SEEK_SET);

  // Explicitly allocate in Octal PSRAM
  void *psram_buf = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
  if (!psram_buf) {
    ESP_LOGE(TAG, "Failed to allocate %d bytes in PSRAM", sz);
    fclose(f);
    return NULL;
  }

  size_t read_bytes = fread(psram_buf, 1, sz, f);
  fclose(f);

  if (read_bytes != sz) {
    ESP_LOGW(TAG, "Read size mismatch for %s", file_path);
  }

  if (out_size)
    *out_size = read_bytes;
  ESP_LOGI(TAG, "Loaded %s (%d KB) into Octal PSRAM", file_path,
           read_bytes / 1024);

  return psram_buf;
}

void asset_free_psram(void *buffer) {
  if (buffer) {
    heap_caps_free(buffer);
  }
}