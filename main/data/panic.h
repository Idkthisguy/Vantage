#pragma once

typedef enum {
  PANIC_KERNEL_ERROR,
  PANIC_GURU_MEDITATION,
  PANIC_BROWNOUT_DETECTED,
  PANIC_OUT_OF_MEMORY,
  PANIC_STORAGE_FAILURE
} panic_code_t;

// Triggers a hard halt with an error screen
void vantage_kernel_panic(panic_code_t code, const char *details);