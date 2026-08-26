#pragma once

#include "esp_err.h"
#include "software/vpk_manifest.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  VPK_OK = 0,
  VPK_ERR_FILE_NOT_FOUND,
  VPK_ERR_MANIFEST_INVALID,
  VPK_ERR_NO_MEM,
  VPK_ERR_SANDBOX_CREATE,
  VPK_ERR_FILE_WRITE,
  VPK_ERR_REGISTRY_FAIL
} vpk_err_t;

const char *vpk_err_to_str(vpk_err_t err);

// Inspects a .vpk archive at src_vpk_path and parses manifest without
// extracting
esp_err_t vpk_inspect_package(const char *src_vpk_path,
                              vpk_manifest_t *out_manifest);

// Extracts .vpk to target storage mount, sandboxes storage, and registers app
vpk_err_t vpk_install_package(const char *src_vpk_path,
                              const char *target_base_path);

#ifdef __cplusplus
}
#endif