#include "vpk_installer.h"
#include "app_registry.h"
#include "drivers/storage.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PSRAM_CHUNK_SIZE (64 * 1024)
#define SRAM_WORK_SIZE (8 * 1024)

static const char *TAG = "VPK_INSTALLER";

typedef struct __attribute__((packed)) {
  char name[100];
  char mode[8];
  char uid[8];
  char gid[8];
  char size[12];
  char mtime[12];
  char chksum[8];
  char typeflag;
  char linkname[100];
  char magic[6];
  char version[2];
  char uname[32];
  char gname[32];
  char devmajor[8];
  char devminor[8];
  char prefix[155];
  char padding[12];
} tar_header_t;

static size_t parse_octal(const char *str, size_t len) {
  char buf[16] = {0};
  memcpy(buf, str, len < 15 ? len : 15);
  return (size_t)strtoul(buf, NULL, 8);
}

const char *vpk_err_to_str(vpk_err_t err) {
  switch (err) {
  case VPK_OK:
    return "Installation successful!";
  case VPK_ERR_FILE_NOT_FOUND:
    return "Package file missing or unreadable";
  case VPK_ERR_MANIFEST_INVALID:
    return "Invalid or missing manifest.json";
  case VPK_ERR_NO_MEM:
    return "Out of memory (PSRAM/SRAM)";
  case VPK_ERR_SANDBOX_CREATE:
    return "Failed to create app directories";
  case VPK_ERR_FILE_WRITE:
    return "Failed writing files to storage";
  case VPK_ERR_REGISTRY_FAIL:
    return "Failed to register application";
  default:
    return "Unknown installation error";
  }
}

static void extract_base_mount(const char *full_path, char *out_mount,
                               size_t max_len) {
  if (strncmp(full_path, "/int", 4) == 0) {
    strncpy(out_mount, "/int", max_len - 1);
  } else {
    strncpy(out_mount, "/sdcard", max_len - 1);
  }
}

static const char *sanitize_tar_path(const char *path) {
  if (!path)
    return "";
  while (*path == '.' || *path == '/') {
    if (*path == '.' && *(path + 1) == '/') {
      path += 2;
    } else if (*path == '/') {
      path++;
    } else {
      break;
    }
  }
  return path;
}

// Strips redundant app_id folder prefix if archive was zipped as a directory
static const char *strip_app_id_prefix(const char *path, const char *app_id) {
  if (!path || !app_id)
    return path;
  size_t id_len = strlen(app_id);
  if (strncmp(path, app_id, id_len) == 0 && path[id_len] == '/') {
    return path + id_len + 1;
  }
  return path;
}

static void make_parent_dirs(const char *file_path) {
  char *temp = (char *)malloc(512);
  if (!temp)
    return;

  snprintf(temp, 512, "%s", file_path);
  char *p = strrchr(temp, '/');
  if (!p) {
    free(temp);
    return;
  }
  *p = '\0';

  for (char *s = temp + 1; *s; s++) {
    if (*s == '/') {
      *s = '\0';
      struct stat st;
      if (stat(temp, &st) != 0) {
        mkdir(temp, 0775);
      }
      *s = '/';
    }
  }

  struct stat st;
  if (stat(temp, &st) != 0) {
    mkdir(temp, 0775);
  }

  free(temp);
}

esp_err_t vpk_inspect_package(const char *src_vpk_path,
                              vpk_manifest_t *out_manifest) {
  if (!src_vpk_path || !out_manifest)
    return ESP_ERR_INVALID_ARG;

  FILE *f = fopen(src_vpk_path, "rb");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open archive: %s", src_vpk_path);
    return ESP_ERR_NOT_FOUND;
  }

  tar_header_t header;
  bool found = false;

  while (fread(&header, 1, sizeof(tar_header_t), f) == sizeof(tar_header_t)) {
    if (header.name[0] == '\0')
      break;

    size_t file_size = parse_octal(header.size, sizeof(header.size));

    char raw_name[256] = {0};
    if (header.prefix[0] != '\0') {
      snprintf(raw_name, sizeof(raw_name), "%.155s/%.100s", header.prefix,
               header.name);
    } else {
      snprintf(raw_name, sizeof(raw_name), "%.100s", header.name);
    }

    const char *clean_name = sanitize_tar_path(raw_name);
    const char *filename = strrchr(clean_name, '/');
    filename = filename ? filename + 1 : clean_name;

    // Matches manifest.json regardless of path prefix
    if (strcmp(filename, "manifest.json") == 0) {
      char *json_str =
          (char *)heap_caps_malloc(file_size + 1, MALLOC_CAP_SPIRAM);
      if (!json_str) {
        fclose(f);
        return ESP_ERR_NO_MEM;
      }

      fread(json_str, 1, file_size, f);
      json_str[file_size] = '\0';

      found = manifest_parse_json(json_str, out_manifest);
      heap_caps_free(json_str);
      break;
    }

    size_t padding = (512 - (file_size % 512)) % 512;
    fseek(f, file_size + padding, SEEK_CUR);
  }

  fclose(f);
  return found ? ESP_OK : ESP_ERR_NOT_FOUND;
}

vpk_err_t vpk_install_package(const char *src_vpk_path,
                              const char *target_base_path) {
  vpk_manifest_t manifest = {0};
  esp_err_t err = vpk_inspect_package(src_vpk_path, &manifest);
  if (err != ESP_OK) {
    return VPK_ERR_MANIFEST_INVALID;
  }

  char base_mount[32];
  if (target_base_path) {
    strncpy(base_mount, target_base_path, sizeof(base_mount) - 1);
    base_mount[sizeof(base_mount) - 1] = '\0';
  } else {
    extract_base_mount(src_vpk_path, base_mount, sizeof(base_mount));
  }

  FILE *f = fopen(src_vpk_path, "rb");
  if (!f)
    return VPK_ERR_FILE_NOT_FOUND;

  uint8_t *psram_buf =
      (uint8_t *)heap_caps_malloc(PSRAM_CHUNK_SIZE, MALLOC_CAP_SPIRAM);
  uint8_t *sram_buf = (uint8_t *)heap_caps_malloc(
      SRAM_WORK_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  if (!psram_buf || !sram_buf) {
    if (psram_buf)
      heap_caps_free(psram_buf);
    if (sram_buf)
      heap_caps_free(sram_buf);
    fclose(f);
    return VPK_ERR_NO_MEM;
  }

  char app_dir[256];
  manifest_get_app_dir(base_mount, manifest.id, app_dir, sizeof(app_dir));

  // Pre-clean app directory to ensure seamless update overwrites
  struct stat st;
  if (stat(app_dir, &st) == 0) {
    storage_remove_recursive(app_dir);
  }

  if (app_sandbox_create_directories(base_mount, manifest.id) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create sandbox directories for %s", manifest.id);
    heap_caps_free(psram_buf);
    heap_caps_free(sram_buf);
    fclose(f);
    return VPK_ERR_SANDBOX_CREATE;
  }

  tar_header_t header;

  while (fread(&header, 1, sizeof(tar_header_t), f) == sizeof(tar_header_t)) {
    if (header.name[0] == '\0')
      break;

    size_t file_size = parse_octal(header.size, sizeof(header.size));

    char raw_rel[256] = {0};
    if (header.prefix[0] != '\0') {
      snprintf(raw_rel, sizeof(raw_rel), "%.155s/%.100s", header.prefix,
               header.name);
    } else {
      snprintf(raw_rel, sizeof(raw_rel), "%.100s", header.name);
    }

    const char *clean_rel = sanitize_tar_path(raw_rel);
    clean_rel = strip_app_id_prefix(clean_rel, manifest.id);

    if (strlen(clean_rel) == 0) {
      size_t padding = (512 - (file_size % 512)) % 512;
      fseek(f, file_size + padding, SEEK_CUR);
      continue;
    }

    char dest_path[512];
    snprintf(dest_path, sizeof(dest_path), "%s/%s", app_dir, clean_rel);

    if (header.typeflag == '5' || clean_rel[strlen(clean_rel) - 1] == '/') {
      mkdir(dest_path, 0775);
    } else {
      make_parent_dirs(dest_path);

      FILE *out_f = fopen(dest_path, "wb");
      if (out_f) {
        size_t remaining = file_size;
        size_t total_written = 0;

        while (remaining > 0) {
          size_t psram_read_target =
              (remaining > PSRAM_CHUNK_SIZE) ? PSRAM_CHUNK_SIZE : remaining;
          size_t psram_bytes_read = fread(psram_buf, 1, psram_read_target, f);

          if (psram_bytes_read == 0) {
            ESP_LOGE(TAG, "Unexpected EOF reading raw stream: %s", clean_rel);
            break;
          }

          size_t processed = 0;

          while (processed < psram_bytes_read) {
            size_t block_size = (psram_bytes_read - processed > SRAM_WORK_SIZE)
                                    ? SRAM_WORK_SIZE
                                    : (psram_bytes_read - processed);

            memcpy(sram_buf, psram_buf + processed, block_size);
            fwrite(sram_buf, 1, block_size, out_f);

            processed += block_size;
            total_written += block_size;

            if (total_written >= PSRAM_CHUNK_SIZE) {
              vTaskDelay(pdMS_TO_TICKS(1));
              total_written = 0;
            }
          }

          remaining -= psram_bytes_read;
        }

        fclose(out_f);
      } else {
        ESP_LOGE(TAG, "Failed to open dest path: %s", dest_path);
        fseek(f, file_size, SEEK_CUR);
      }

      size_t padding = (512 - (file_size % 512)) % 512;
      if (padding > 0)
        fseek(f, padding, SEEK_CUR);
    }
  }

  heap_caps_free(psram_buf);
  heap_caps_free(sram_buf);
  fclose(f);

  if (app_registry_add(base_mount, &manifest) != ESP_OK) {
    return VPK_ERR_REGISTRY_FAIL;
  }

  return VPK_OK;
}
