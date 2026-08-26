#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


#define MAX_PATH_LEN 128
#define MAX_ID_LEN 32

typedef struct {
  char id[MAX_ID_LEN];
  char name[64];
  char version[16];
  char exec[MAX_PATH_LEN];
  char icon[MAX_PATH_LEN];
} vpk_manifest_t;

bool manifest_parse_json(const char *json_string, vpk_manifest_t *out_manifest);

// Dynamically builds paths based on target storage root ("/int" or "/sdcard")
void manifest_get_app_dir(const char *base_path, const char *app_id,
                          char *out_path, size_t max_len);
void manifest_get_data_dir(const char *base_path, const char *app_id,
                           char *out_path, size_t max_len);