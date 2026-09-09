#include "sdk_bridge.h"

size_t sim_portable_app_count(void) {
  return 0;
}

const vantage_app_desc_t *sim_portable_app_at(size_t index) {
  (void)index;
  return NULL;
}
