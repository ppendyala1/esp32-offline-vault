#pragma once
#include "driver/gpio.h"

// Adafruit Feather ESP32-S3 PID 5477. Numbers are ESP GPIOs, not pad indices.
namespace board {
constexpr gpio_num_t TFT_SCK = GPIO_NUM_36;
constexpr gpio_num_t TFT_MOSI = GPIO_NUM_35;
constexpr gpio_num_t TFT_CS = GPIO_NUM_6;
constexpr gpio_num_t TFT_DC = GPIO_NUM_5;
constexpr gpio_num_t TFT_RST = GPIO_NUM_9;
constexpr gpio_num_t TFT_BL = GPIO_NUM_10;
constexpr gpio_num_t SDA = GPIO_NUM_3;
constexpr gpio_num_t SCL = GPIO_NUM_4;
constexpr gpio_num_t I2C_POWER = GPIO_NUM_7;
// GPIO13 has the onboard LED load; use A3/GPIO15 for OK instead.
constexpr gpio_num_t BUTTONS[] = {GPIO_NUM_11, GPIO_NUM_12, GPIO_NUM_15, GPIO_NUM_14};
} // namespace board
