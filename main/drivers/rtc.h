#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

class DS3231 {
public:
    DS3231() = default;
    ~DS3231();

    DS3231(const DS3231&) = delete;
    DS3231& operator=(const DS3231&) = delete;

    // Create the I2C bus and connect to the RTC.
    esp_err_t begin(gpio_num_t sda, gpio_num_t scl);

    // Read UTC Unix time.
    // Reject invalid dates, oscillator stop, or incomplete time updates.
    // Alarm 2 minute register is reserved as a persistent validity journal.
    esp_err_t read_unix(uint64_t& seconds);

    // Set UTC Unix time, supporting years 2000–2099.
    esp_err_t set_unix(uint64_t seconds);

private:
    esp_err_t read_registers(
        uint8_t address, uint8_t* output, size_t length);

    esp_err_t write_registers(
        uint8_t address, const uint8_t* data, size_t length);

    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t device_ = nullptr;
};
