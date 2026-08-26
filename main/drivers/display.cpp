#define LGFX_USE_V1
#include "display.h"
#include "data/gpio_pins.h"
#include "lvgl.h"
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9488 _panel_instance;
  lgfx::Bus_SPI _bus_instance;
  lgfx::Touch_XPT2046 _touch_instance;

public:
  LGFX() {
    // Display Bus Config
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 80000000;
      cfg.freq_read = 16000000;
      cfg.pin_sclk = DISPLAY_PINS.sclk;
      cfg.pin_mosi = DISPLAY_PINS.mosi;
      cfg.pin_miso = DISPLAY_PINS.miso;
      cfg.pin_dc = DISPLAY_PINS.dc;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    // Panel Config
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = DISPLAY_PINS.cs;
      cfg.pin_rst = DISPLAY_PINS.rst;
      cfg.panel_width = 320;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.readable = false;
      cfg.invert = false;
      cfg.rgb_order = false;

      _panel_instance.config(cfg);
    }

    // Touch Config
    {
      auto cfg = _touch_instance.config();
      cfg.spi_host = -1;
      cfg.pin_cs = TOUCH_PINS.cs;
      cfg.pin_sclk = TOUCH_PINS.sclk;
      cfg.pin_mosi = TOUCH_PINS.mosi;
      cfg.pin_miso = TOUCH_PINS.miso;
      cfg.pin_int = -1;
      cfg.bus_shared = false;
      cfg.freq = 1000000;

      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }

    setPanel(&_panel_instance);
  }
};

static LGFX tft;
static lv_color_t
    buf1[320 * 20]; // Buffer sized to match width (320px * 20 lines)

static void display_flush_cb(lv_display_t *disp, const lv_area_t *area,
                             uint8_t *px_map) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.writePixels((uint16_t *)px_map, w * h, true);
  tft.endWrite();

  lv_display_flush_ready(disp);
}

static void touchpad_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  uint16_t touchX, touchY;
  bool touched = tft.getTouch(&touchX, &touchY);

  if (touched) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touchX;
    data->point.y = touchY;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

extern "C" void display_init(void) {
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  uint16_t calData[8] = {215, 3913, 218, 246, 3777, 3895, 3831, 207};
  tft.setTouchCalibrate(calData);
}

extern "C" void display_lvgl_init(void) {
  lv_init();

  lv_display_t *disp = lv_display_create(320, 480);

  lv_display_set_buffers(disp, buf1, NULL, sizeof(buf1),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, display_flush_cb);

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchpad_read_cb);
}

extern "C" void display_draw_raw_panic(const char *title, const char *details,
                                       const uint8_t *icon_16x16,
                                       uint16_t bg_color, uint16_t fg_color,
                                       const uint8_t font[128][8]) {
  tft.fillScreen(bg_color);

  int icon_x = 128; // Centered on 320px width
  int icon_y = 40;
  int scale = 4;

  for (int y = 0; y < 16; y++) {
    uint8_t byte1 = icon_16x16[y * 2];
    uint8_t byte2 = icon_16x16[y * 2 + 1];
    uint16_t row = (byte1 << 8) | byte2;

    for (int x = 0; x < 16; x++) {
      if (row & (1 << (15 - x))) {
        tft.fillRect(icon_x + (x * scale), icon_y + (y * scale), scale, scale,
                     fg_color);
      }
    }
  }

  int text_x = 20;
  int text_y = 130;
  for (int i = 0; title[i] != '\0'; i++) {
    char c = title[i];
    if (c >= 128)
      continue;

    for (int py = 0; py < 8; py++) {
      uint8_t line = font[(uint8_t)c][py];
      for (int px = 0; px < 8; px++) {
        if (line & (1 << (7 - px))) {
          tft.fillRect(text_x + (px * 2), text_y + (py * 2), 2, 2, fg_color);
        }
      }
    }
    text_x += 18; // Advance horizontally
  }

  text_x = 20;
  text_y = 180;
  for (int i = 0; details[i] != '\0'; i++) {
    char c = details[i];
    if (c == '\n') {
      text_x = 20;
      text_y += 16;
      continue;
    }
    if (c >= 128)
      continue;

    for (int py = 0; py < 8; py++) {
      uint8_t line = font[(uint8_t)c][py];
      for (int px = 0; px < 8; px++) {
        if (line & (1 << (7 - px))) {
          tft.drawPixel(text_x + px, text_y + py, fg_color);
        }
      }
    }
    text_x += 9;
    if (text_x > 300) { // Screen wrap
      text_x = 20;
      text_y += 12;
    }
  }
}