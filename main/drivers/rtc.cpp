#include "rtc.h"

#include <string.h>

namespace {

constexpr uint8_t RTC_ADDRESS = 0x68;
constexpr uint8_t CONTROL_REG = 0x0E;
constexpr uint8_t STATUS_REG = 0x0F;
constexpr uint8_t OSCILLATOR_STOP = 0x80;
// Alarm 2 is unused by this application. Its minute register is a persistent
// update journal, using valid masked BCD values (never an invalid calendar).
constexpr uint8_t JOURNAL_REG = 0x0B;
constexpr uint8_t TIME_VALID = 0xA5;
constexpr uint8_t TIME_DIRTY = 0x80;

constexpr int TIMEOUT_MS = 100;
constexpr uint64_t UNIX_2000 = 946684800ULL;
constexpr uint64_t UNIX_2100 = 4102444800ULL;

bool leap_year(unsigned year)
{
    return year % 4 == 0 &&
           (year % 100 != 0 || year % 400 == 0);
}

unsigned month_days(unsigned year, unsigned month)
{
    static constexpr uint8_t days[] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    if (month < 1 || month > 12) {
        return 0;
    }

    return days[month - 1] +
           ((month == 2 && leap_year(year)) ? 1 : 0);
}

uint8_t to_bcd(unsigned value)
{
    return static_cast<uint8_t>(
        ((value / 10) << 4) | (value % 10));
}

bool from_bcd(uint8_t value, unsigned& output)
{
    unsigned low = value & 0x0F;
    unsigned high = value >> 4;

    if (low > 9 || high > 9) {
        return false;
    }

    output = high * 10 + low;
    return true;
}

} // namespace

DS3231::~DS3231()
{
    if (device_) {
        i2c_master_bus_rm_device(device_);
    }

    if (bus_) {
        i2c_del_master_bus(bus_);
    }
}

esp_err_t DS3231::begin(gpio_num_t sda, gpio_num_t scl)
{
    if (bus_ || device_) {
        return ESP_ERR_INVALID_STATE;
    }

    i2c_master_bus_config_t config = {};
    config.i2c_port = -1;
    config.sda_io_num = sda;
    config.scl_io_num = scl;
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.glitch_ignore_cnt = 7;
    config.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&config, &bus_);
    if (err != ESP_OK) {
        return err;
    }

    i2c_device_config_t device_config = {};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = RTC_ADDRESS;
    device_config.scl_speed_hz = 100000;

    err = i2c_master_bus_add_device(
        bus_, &device_config, &device_);

    if (err == ESP_OK) {
        err = i2c_master_probe(bus_, RTC_ADDRESS, TIMEOUT_MS);
    }

    if (err != ESP_OK) {
        if (device_) {
            i2c_master_bus_rm_device(device_);
            device_ = nullptr;
        }

        i2c_del_master_bus(bus_);
        bus_ = nullptr;
    }

    return err;
}

esp_err_t DS3231::read_registers(
    uint8_t address, uint8_t* output, size_t length)
{
    if (!device_) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!output || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_transmit_receive(
        device_, &address, 1, output, length, TIMEOUT_MS);
}

esp_err_t DS3231::write_registers(
    uint8_t address, const uint8_t* data, size_t length)
{
    if (!device_) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!data || length == 0 || length > 7) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buffer[8] = {};
    buffer[0] = address;
    memcpy(buffer + 1, data, length);

    return i2c_master_transmit(
        device_, buffer, length + 1, TIMEOUT_MS);
}

esp_err_t DS3231::read_unix(uint64_t& seconds)
{
    seconds = 0;

    uint8_t status = 0;
    esp_err_t err = read_registers(STATUS_REG, &status, 1);

    if (err != ESP_OK) {
        return err;
    }

    if (status & OSCILLATOR_STOP) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t journal = 0;
    err = read_registers(JOURNAL_REG, &journal, 1);
    if (err != ESP_OK) return err;
    if (journal != TIME_VALID) return ESP_ERR_INVALID_STATE;

    uint8_t registers[7] = {};
    err = read_registers(0x00, registers, sizeof(registers));

    if (err != ESP_OK) {
        return err;
    }

    unsigned second, minute, hour;
    unsigned day, month, year;

    // Reject reserved bits and dates beyond 2099.
    if ((registers[0] & 0x80) ||
        (registers[1] & 0x80) ||
        (registers[2] & 0x80) ||
        (registers[4] & 0xC0) ||
        (registers[5] & 0xE0) ||
        registers[3] < 1 || registers[3] > 7) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (!from_bcd(registers[0], second) ||
        !from_bcd(registers[1], minute) ||
        !from_bcd(registers[4], day) ||
        !from_bcd(registers[5], month) ||
        !from_bcd(registers[6], year)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (registers[2] & 0x40) {
        // Convert 12-hour format to 24-hour format.
        if (!from_bcd(registers[2] & 0x1F, hour) ||
            hour < 1 || hour > 12) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        hour = hour % 12 +
               ((registers[2] & 0x20) ? 12 : 0);
    } else {
        if (!from_bcd(registers[2] & 0x3F, hour)) {
            return ESP_ERR_INVALID_RESPONSE;
        }
    }

    year += 2000;

    if (second > 59 || minute > 59 || hour > 23 ||
        month < 1 || month > 12 ||
        day < 1 || day > month_days(year, month)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint64_t days = 0;

    for (unsigned y = 2000; y < year; ++y) {
        days += leap_year(y) ? 366 : 365;
    }

    for (unsigned m = 1; m < month; ++m) {
        days += month_days(year, m);
    }

    days += day - 1;

    // Check again in case the oscillator stopped during the read.
    err = read_registers(STATUS_REG, &status, 1);
    if (err != ESP_OK) {
        return err;
    }

    if (status & OSCILLATOR_STOP) {
        return ESP_ERR_INVALID_STATE;
    }

    seconds = UNIX_2000 + days * 86400ULL +
              hour * 3600ULL + minute * 60ULL + second;

    return ESP_OK;
}

esp_err_t DS3231::set_unix(uint64_t seconds)
{
    if (seconds < UNIX_2000 || seconds >= UNIX_2100) {
        return ESP_ERR_INVALID_ARG;
    }

    uint64_t elapsed = seconds - UNIX_2000;
    unsigned days = static_cast<unsigned>(elapsed / 86400);
    unsigned remaining = days;

    unsigned year = 2000;
    while (remaining >= (leap_year(year) ? 366U : 365U)) {
        remaining -= leap_year(year) ? 366 : 365;
        ++year;
    }

    unsigned month = 1;
    while (remaining >= month_days(year, month)) {
        remaining -= month_days(year, month);
        ++month;
    }

    unsigned day = remaining + 1;
    unsigned within_day = static_cast<unsigned>(elapsed % 86400);
    unsigned hour = within_day / 3600;
    unsigned minute = (within_day % 3600) / 60;
    unsigned second = within_day % 60;

    uint8_t status = 0;
    esp_err_t err = read_registers(STATUS_REG, &status, 1);
    if (err != ESP_OK) {
        return err;
    }

    // OSF cannot be set by software. Mark the update in a battery-backed alarm
    // register first, so a reset/partial time write remains invalid on reboot.
    err = write_registers(JOURNAL_REG, &TIME_DIRTY, 1);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t control = 0;
    err = read_registers(CONTROL_REG, &control, 1);
    if (err != ESP_OK) {
        return err;
    }

    // Enable backup oscillator operation; reserve/disable Alarm 2 interrupts.
    control &= static_cast<uint8_t>(~0x82);
    err = write_registers(CONTROL_REG, &control, 1);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t registers[7] = {
        to_bcd(second),
        to_bcd(minute),
        to_bcd(hour),
        static_cast<uint8_t>((days + 6) % 7 + 1),
        to_bcd(day),
        to_bcd(month),
        to_bcd(year - 2000)
    };

    err = write_registers(0x00, registers, sizeof(registers));
    if (err != ESP_OK) {
        return err;
    }

    err = write_registers(JOURNAL_REG, &TIME_VALID, 1);
    if (err != ESP_OK) {
        return err;
    }

    err = read_registers(STATUS_REG, &status, 1);
    if (err != ESP_OK) {
        return err;
    }

    status = static_cast<uint8_t>(
        (status & ~OSCILLATOR_STOP) | 0x03);

    return write_registers(STATUS_REG, &status, 1);
}
