#pragma once
typedef int gpio_num_t;
#include <stdint.h>
#include "esp_err.h"
enum { GPIO_NUM_3=3, GPIO_NUM_4=4, GPIO_NUM_5=5, GPIO_NUM_6=6, GPIO_NUM_7=7,
       GPIO_NUM_9=9, GPIO_NUM_10=10, GPIO_NUM_11=11, GPIO_NUM_12=12, GPIO_NUM_13=13,
       GPIO_NUM_14=14, GPIO_NUM_15=15, GPIO_NUM_35=35, GPIO_NUM_36=36 };
constexpr int GPIO_MODE_INPUT=0, GPIO_MODE_OUTPUT=1, GPIO_PULLUP_ENABLE=1;
struct gpio_config_t { uint64_t pin_bit_mask; int mode, pull_up_en; };
esp_err_t gpio_config(const gpio_config_t*);
esp_err_t gpio_set_level(gpio_num_t, int);
