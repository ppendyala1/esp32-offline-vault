#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef void* i2c_master_bus_handle_t;
typedef void* i2c_master_dev_handle_t;
constexpr int I2C_CLK_SRC_DEFAULT = 0, I2C_ADDR_BIT_LEN_7 = 0;
struct i2c_master_bus_config_t {
    int i2c_port, sda_io_num, scl_io_num, clk_source, glitch_ignore_cnt;
    struct { bool enable_internal_pullup; } flags;
};
struct i2c_device_config_t { int dev_addr_length, device_address, scl_speed_hz; };
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t*, i2c_master_bus_handle_t*);
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t);
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t, const i2c_device_config_t*, i2c_master_dev_handle_t*);
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t);
esp_err_t i2c_master_probe(i2c_master_bus_handle_t, uint16_t, int);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t, const uint8_t*, size_t, uint8_t*, size_t, int);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t, const uint8_t*, size_t, int);
