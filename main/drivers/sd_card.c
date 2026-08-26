#include "sd_card.h"
#include "data/gpio_pins.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include <stdio.h>
#include <sys/stat.h>

static const char *TAG = "FS_SYSTEM";
static sdmmc_card_t *card = NULL;

esp_err_t fs_init(void) {
  ESP_LOGI(TAG, "Mounting SD Card FileSystem...");

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 10,
      .allocation_unit_size = 16 * 1024,
      .disk_status_check_enable = true};

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI3_HOST;

  spi_bus_config_t bus_cfg = {
      .mosi_io_num = SD_PINS.mosi,
      .miso_io_num = SD_PINS.miso,
      .sclk_io_num = SD_PINS.sclk,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 16384, // Higher transfer size for faster streaming
  };

  esp_err_t ret = spi_bus_initialize(SPI3_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
  if (ret != ESP_OK)
    return ret;

  sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config.gpio_cs = SD_PINS.cs;
  slot_config.host_id = SPI3_HOST;

  ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config,
                                &card);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to mount FAT32 filesystem: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "Filesystem Mounted Successfully at /sdcard");
  return ESP_OK;
}

void fs_create_default_dirs(void) {
  mkdir("/sdcard/system", 0775);
  mkdir("/sdcard/assets", 0775);
  mkdir("/sdcard/assets/images", 0775);
  mkdir("/sdcard/assets/fonts", 0775);
  mkdir("/sdcard/assets/videos", 0775);
  mkdir("/sdcard/update", 0775);
  ESP_LOGI(TAG, "VantageOS folder structure verified.");
}