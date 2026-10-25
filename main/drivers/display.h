#pragma once
#include "driver/spi_master.h"
#include "esp_err.h"

class Display {
public:
    esp_err_t begin();
    void clear();
    void text(int x, int y, const char* value, int scale = 1);
    // Blocking transfer: no DMA or driver-owned copy of sensitive pixels.
    esp_err_t present();
private:
    esp_err_t command(uint8_t cmd, const uint8_t* bytes = nullptr, size_t size = 0);
    spi_device_handle_t spi_ = nullptr;
    uint16_t pixels_[240 * 135] = {};
};
