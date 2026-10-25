#pragma once

#include "vault.h"
#include "esp_partition.h"

class EspFlash final : public vault::Flash {
public:
    EspFlash()
        : partition_(esp_partition_find_first(
              ESP_PARTITION_TYPE_DATA,
              static_cast<esp_partition_subtype_t>(0x40),
              "vault")) {}

    bool available() const {
        return partition_ != nullptr;
    }

    uint32_t size() const override {
        return partition_ ? partition_->size : 0;
    }

    bool read(uint32_t offset,
              void* out, size_t length) override {
        if (!in_bounds(offset, length) ||
            (!out && length != 0)) {
            return false;
        }

        if (length == 0) return true;

        return esp_partition_read(
            partition_, offset, out, length
        ) == ESP_OK;
    }

    bool write(uint32_t offset,
               const void* data, size_t length) override {
        if (!in_bounds(offset, length) ||
            (!data && length != 0)) {
            return false;
        }

        if (length == 0) return true;

        return esp_partition_write(
            partition_, offset, data, length
        ) == ESP_OK;
    }

    bool erase(uint32_t offset,
               size_t length) override {
        if (!in_bounds(offset, length) ||
            offset % 4096 != 0 ||
            length % 4096 != 0) {
            return false;
        }

        if (length == 0) return true;

        return esp_partition_erase_range(
            partition_, offset, length
        ) == ESP_OK;
    }

private:
    bool in_bounds(uint32_t offset, size_t length) const {
        return partition_ &&
               offset <= partition_->size &&
               length <= partition_->size - offset;
    }

    const esp_partition_t* partition_;
};