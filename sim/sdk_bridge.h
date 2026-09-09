#pragma once

#include "data/vantage_abi.h"
#include "wm.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Converts a portable app descriptor into a desktop window-manager app. */
const app_descriptor_t *sim_sdk_wrap_app(const vantage_app_desc_t *app);

/* Keeps portable app data paths bound to whichever app has the spotlight. */
void sim_sdk_focus_app(const app_descriptor_t *app);

/* Registry compiled into the simulator. Add portable app sources there. */
size_t sim_portable_app_count(void);
const vantage_app_desc_t *sim_portable_app_at(size_t index);

#ifdef __cplusplus
}
#endif
