#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Animate a source object expanding/fading into a target object.
 *
 * The target fades in while a ghost copy of the source expands to its position.
 * The ghost is deleted when the animation completes.
 *
 * @param src    Source object to animate from. Must be on the active screen.
 * @param target Target object to reveal.
 */
void wm_animate_launch(lv_obj_t *src, lv_obj_t *target);

#ifdef __cplusplus
}
#endif
