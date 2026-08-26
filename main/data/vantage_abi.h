#pragma once

#include <stdbool.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif

// OS-native lifecycle function pointer definitions
typedef struct {
  void (*on_create)(void *window_obj);
  void (*on_resume)(void);
  void (*on_pause)(void);
  void (*on_destroy)(void);
} vantage_app_lifecycle_t;

// OS-native ABI binary descriptor
typedef struct {
  const char *id;
  const char *name;
  const char *version;
  vantage_app_lifecycle_t lifecycle;
} vantage_app_desc_t;

#ifdef __cplusplus
}
#endif