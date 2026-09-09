#include "lvgl.h"
#include "wm.h"
#include <SDL2/SDL.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  lv_init();

  lv_display_t *disp = lv_sdl_window_create(320, 480);
  if (!disp) {
    printf("Failed to create SDL display\n");
    return 1;
  }

  lv_indev_t *mouse = lv_sdl_mouse_create();
  lv_indev_t *kbd = lv_sdl_keyboard_create();
  (void)mouse;
  (void)kbd;

  wm_init();

  while (1) {
    uint32_t delay_ms = lv_timer_handler();
    if (delay_ms < 1)
      delay_ms = 1;
    if (delay_ms > 500)
      delay_ms = 500;

    SDL_Delay(delay_ms);
  }

  return 0;
}
