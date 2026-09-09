#ifndef LV_CONF_H
#define LV_CONF_H

/* Base configuration for the Vantage PC simulator.
 * All other LVGL options fall back to the defaults in lv_conf_internal.h. */

#define LV_COLOR_DEPTH 32
#define LV_MEM_SIZE (512 * 1024U)

#define LV_USE_OS LV_OS_WINDOWS

/* Use the SDL window/mouse/keyboard driver. */
#define LV_USE_SDL 1
#define LV_SDL_INCLUDE_PATH <SDL2/SDL.h>

/* Logging */
#define LV_USE_LOG 1
#define LV_LOG_PRINTF 1

/* Fonts used by the simulator UI. */
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_24 1

/* Disable heavy optional subsystems for faster simulator builds. */
#define LV_USE_PERF_MONITOR 0
#define LV_USE_SYSMON 0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_MUSIC 0

#endif /* LV_CONF_H */
