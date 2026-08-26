#include "drivers/storage.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "tinyusb.h"
#include "tusb_msc_storage.h"
#include "vfs_fat_internal.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>


static const char *TAG = "STORAGE_MGR";
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
static bool s_usb_mounted = false;

// Initialize Internal Flash as Primary Storage (/int)
esp_err_t dual_storage_init(void) {
  ESP_LOGI(TAG, "Initializing Primary Internal Flash FATFS...");

  const esp_vfs_fat_mount_config_t mount_config = {
      .max_files = 5,
      .format_if_mount_failed = true,
      .allocation_unit_size = CONFIG_WL_SECTOR_SIZE};

  esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl("/int", "storage",
                                                   &mount_config, &s_wl_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to mount internal flash FATFS (%s)",
             esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG, "Primary Storage mounted at /int");
  return ESP_OK;
}

// Write to storage safely using PSRAM buffer to mitigate wear & tear
esp_err_t storage_psram_flush_write(const char *path, const void *data,
                                    size_t len) {
  if (s_usb_mounted) {
    ESP_LOGW(TAG, "USB MSC Active! Writing halted to prevent FS corruption.");
    return ESP_ERR_INVALID_STATE;
  }

  // Allocate buffer in Octal PSRAM
  void *psram_buf = heap_caps_malloc(len > 0 ? len : 1, MALLOC_CAP_SPIRAM);
  if (!psram_buf) {
    ESP_LOGE(TAG, "Failed to allocate PSRAM write buffer");
    return ESP_ERR_NO_MEM;
  }

  if (len > 0 && data) {
    memcpy(psram_buf, data, len);
  }

  FILE *f = fopen(path, "wb");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open path: %s", path);
    heap_caps_free(psram_buf);
    return ESP_FAIL;
  }

  if (len > 0) {
    fwrite(psram_buf, 1, len, f);
  }
  fclose(f);

  heap_caps_free(psram_buf);
  return ESP_OK;
}

// PSRAM-based Recursive File/Directory Deletion
esp_err_t storage_remove_recursive(const char *path) {
  struct stat st;
  if (stat(path, &st) != 0)
    return ESP_ERR_NOT_FOUND;

  if (S_ISREG(st.st_mode)) {
    return (unlink(path) == 0) ? ESP_OK : ESP_FAIL;
  }

  if (S_ISDIR(st.st_mode)) {
    DIR *dir = opendir(path);
    if (!dir)
      return ESP_FAIL;

    struct dirent *entry;
    char subpath[300];
    while ((entry = readdir(dir)) != NULL) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        continue;
      snprintf(subpath, sizeof(subpath), "%s/%s", path, entry->d_name);
      storage_remove_recursive(subpath);
    }
    closedir(dir);
    return (rmdir(path) == 0) ? ESP_OK : ESP_FAIL;
  }
  return ESP_FAIL;
}

static void usb_msc_mount_cb(tinyusb_msc_storage_handle_t handle,
                             tinyusb_msc_event_t *event, void *user_ctx) {
  if (event) {
    s_usb_mounted = event->mount_changed_data.is_mounted;
    if (s_usb_mounted) {
      ESP_LOGW(TAG, "PC connected! Internal FS mounted as USB Drive.");
    } else {
      ESP_LOGI(TAG, "PC disconnected. Internal FS restored for ESP.");
    }
  }
}

esp_err_t storage_enable_usb_msc(void) {
  ESP_LOGI(TAG, "Initializing USB MSC for Internal Storage...");
  const tinyusb_config_t tusb_cfg = {0};
  ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

  tinyusb_msc_spiflash_config_t config = {
      .wl_handle = s_wl_handle,
      .callback_mount_changed = usb_msc_mount_cb,
  };

  return tinyusb_msc_storage_init_spiflash(&config);
}