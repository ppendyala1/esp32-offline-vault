#include "display.h"
#include "platform/board.h"
#include "crypto.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

namespace {
// Five column, seven pixel ASCII glyphs, 0x20 through 0x7e.
constexpr uint8_t font[][5] = {
{0,0,0,0,0},{0,0,95,0,0},{0,7,0,7,0},{20,127,20,127,20},{36,42,127,42,18},
{35,19,8,100,98},{54,73,85,34,80},{0,5,3,0,0},{0,28,34,65,0},{0,65,34,28,0},
{20,8,62,8,20},{8,8,62,8,8},{0,80,48,0,0},{8,8,8,8,8},{0,96,96,0,0},{32,16,8,4,2},
{62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},{33,65,69,75,49},{24,20,18,127,16},
{39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},{54,73,73,73,54},{6,73,73,41,30},
{0,54,54,0,0},{0,86,54,0,0},{8,20,34,65,0},{20,20,20,20,20},{0,65,34,20,8},
{2,1,81,9,6},{50,73,121,65,62},{126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},
{127,65,65,34,28},{127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},
{0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},{127,2,12,2,127},
{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},{62,65,81,33,94},{127,9,25,41,70},
{70,73,73,73,49},{1,1,127,1,1},{63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},
{99,20,8,20,99},{7,8,112,8,7},{97,81,73,69,67},{0,127,65,65,0},{2,4,8,16,32},
{0,65,65,127,0},{4,2,1,2,4},{64,64,64,64,64},{0,1,2,4,0},{32,84,84,84,120},
{127,72,68,68,56},{56,68,68,68,32},{56,68,68,72,127},{56,84,84,84,24},{8,126,9,1,2},
{12,82,82,82,62},{127,8,4,4,120},{0,68,125,64,0},{32,64,68,61,0},{127,16,40,68,0},
{0,65,127,64,0},{124,4,24,4,120},{124,8,4,4,120},{56,68,68,68,56},{124,20,20,20,8},
{8,20,20,24,124},{124,8,4,4,8},{72,84,84,84,32},{4,63,68,64,32},{60,64,64,32,124},
{28,32,64,32,28},{60,64,48,64,60},{68,40,16,40,68},{12,80,80,80,60},{68,100,84,76,68},
{0,8,54,65,0},{0,0,127,0,0},{0,65,54,8,0},{8,4,8,16,8}
};
static_assert(sizeof(font) == 95 * 5);
}

esp_err_t Display::command(uint8_t cmd, const uint8_t* bytes, size_t size) {
    gpio_set_level(board::TFT_DC, 0);
    spi_transaction_t transaction = {};
    transaction.length = 8;
    transaction.tx_buffer = &cmd;
    esp_err_t err = spi_device_polling_transmit(spi_, &transaction);
    if (err != ESP_OK || !size) return err;
    gpio_set_level(board::TFT_DC, 1);
    transaction.length = size * 8;
    transaction.tx_buffer = bytes;
    return spi_device_polling_transmit(spi_, &transaction);
}

esp_err_t Display::begin() {
    gpio_config_t pins = {};
    pins.pin_bit_mask = (1ULL << board::TFT_DC) | (1ULL << board::TFT_RST) | (1ULL << board::TFT_BL);
    pins.mode = GPIO_MODE_OUTPUT;
    esp_err_t err = gpio_config(&pins);
    if (err != ESP_OK) return err;
    gpio_set_level(board::TFT_BL, 0);
    spi_bus_config_t bus = {};
    bus.mosi_io_num = board::TFT_MOSI;
    bus.miso_io_num = -1;
    bus.sclk_io_num = board::TFT_SCK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = sizeof(pixels_);
    err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK) return err;
    spi_device_interface_config_t device = {};
    device.clock_speed_hz = 20000000;
    device.mode = 0;
    device.spics_io_num = board::TFT_CS;
    device.queue_size = 1;
    err = spi_bus_add_device(SPI2_HOST, &device, &spi_);
    if (err != ESP_OK) return err;
    gpio_set_level(board::TFT_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(board::TFT_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    if ((err = command(0x01)) != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(150));
    if ((err = command(0x11)) != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(120));
    const uint8_t format = 0x55, rotation = 0x60;
    if ((err = command(0x3a, &format, 1)) != ESP_OK) return err;
    if ((err = command(0x36, &rotation, 1)) != ESP_OK) return err;
    if ((err = command(0x21)) != ESP_OK) return err;
    if ((err = command(0x13)) != ESP_OK) return err;
    if ((err = command(0x29)) != ESP_OK) return err;
    clear();
    err = present();
    if (err == ESP_OK) gpio_set_level(board::TFT_BL, 1);
    return err;
}

void Display::clear() { vault::wipe(pixels_, sizeof(pixels_)); }

void Display::text(int x, int y, const char* value, int scale) {
    const int origin = x;
    while (*value && y + 7 * scale <= 135) {
        unsigned char c = static_cast<unsigned char>(*value++);
        if (c == '\n' || x + 6 * scale > 240) {
            y += 8 * scale; x = origin;
            if (c == '\n') continue;
        }
        if (c < 32 || c > 126) c = '?';
        for (int col = 0; col < 5; ++col)
            for (int row = 0; row < 7; ++row)
                if (font[c - 32][col] & (1 << row))
                    for (int dx = 0; dx < scale; ++dx)
                        for (int dy = 0; dy < scale; ++dy) {
                            const int px = x + col * scale + dx, py = y + row * scale + dy;
                            if (px >= 0 && px < 240 && py >= 0 && py < 135)
                                pixels_[py * 240 + px] = 0xffff;
                        }
        x += 6 * scale;
    }
}

esp_err_t Display::present() {
    // PID 4383: 135x240 glass in a 240x320 ST7789 RAM, landscape offsets.
    const uint8_t columns[] = {0,40,1,23}; // 40..279
    const uint8_t rows[] = {0,53,0,187};   // 53..187
    esp_err_t err = command(0x2a, columns, sizeof(columns));
    if (err == ESP_OK) err = command(0x2b, rows, sizeof(rows));
    // No DMA: chunk below the SPI FIFO limit. ST7789 retains its RAM cursor
    // across CS edges, until another command is issued.
    if (err == ESP_OK) err = command(0x2c);
    gpio_set_level(board::TFT_DC, 1);
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(pixels_);
    for (size_t offset = 0; err == ESP_OK && offset < sizeof(pixels_); offset += 64) {
        spi_transaction_t tx = {};
        tx.length = 8 * ((sizeof(pixels_) - offset < 64) ? sizeof(pixels_) - offset : 64);
        tx.tx_buffer = bytes + offset;
        err = spi_device_polling_transmit(spi_, &tx);
    }
    clear();
    if (err != ESP_OK) gpio_set_level(board::TFT_BL, 0);
    return err;
}
