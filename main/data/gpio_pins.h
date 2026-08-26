#pragma once

// YOU CAN EDIT THESE TO THE PHYSICALLY CONNECTED GPIO PINS OF YOUR BUILD

#include "driver/gpio.h"

// Display SPI Pin Configuration Struct
typedef struct {
  gpio_num_t mosi;
  gpio_num_t miso;
  gpio_num_t sclk;
  gpio_num_t cs;
  gpio_num_t dc;
  gpio_num_t rst;
  // gpio_num_t bl;
} lcd_pins_t;

// Touch SPI Pin Configuration Struct
typedef struct {
  gpio_num_t mosi;
  gpio_num_t miso;
  gpio_num_t sclk;
  gpio_num_t cs;
  gpio_num_t irq;
} touch_pins_t;

typedef struct {
  gpio_num_t mosi;
  gpio_num_t miso;
  gpio_num_t sclk;
  gpio_num_t cs;
} sd_pins_t;

// Global Pin instances (Assign your physical GPIO numbers here)
static const lcd_pins_t DISPLAY_PINS = {
    .mosi = GPIO_NUM_11,
    .miso = GPIO_NUM_13,
    .sclk = GPIO_NUM_12,
    .cs = GPIO_NUM_10,
    .dc = GPIO_NUM_9,
    .rst = GPIO_NUM_14,
    //.bl   = GPIO_NUM_45
};

static const touch_pins_t TOUCH_PINS = {.mosi = GPIO_NUM_17,
                                        .miso = GPIO_NUM_16,
                                        .sclk = GPIO_NUM_18,
                                        .cs = GPIO_NUM_15,
                                        .irq = GPIO_NUM_4};

static const sd_pins_t SD_PINS = {
    .mosi = GPIO_NUM_5,
    .miso = GPIO_NUM_7,
    .sclk = GPIO_NUM_6,
    .cs = GPIO_NUM_3,
};