#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "../core/crypto.h"
#include "../core/totp.h"

#include "flash_esp.h"
#include "admin.h"

static const char* TAG = "vault";

static void run_tests() {
    uint8_t secret[20] = {};
    size_t secret_length = 0;
    uint32_t code = 0;

    const bool totp_ok =
        vault::decode_base32(
            "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ",
            secret, sizeof(secret), secret_length
        ) &&
        vault::generate_totp(
            secret, secret_length,
            59, code, 8, 30, vault::Hash::SHA1
        ) &&
        code == 94287082;

    ESP_LOGI(TAG, "TOTP: %s", totp_ok ? "PASS" : "FAIL");
    vault::wipe(secret, sizeof(secret));

    uint8_t salt[16] = {};
    uint8_t nonce[12] = {};
    uint8_t key[32] = {};
    uint8_t tag[16] = {};

    const uint8_t message[] = "DummyPassword123";
    const size_t length = sizeof(message) - 1;

    uint8_t cipher[sizeof(message)] = {};
    uint8_t recovered[sizeof(message)] = {};

    const bool encrypted = vault::random_bytes(salt, sizeof(salt)) && vault::random_bytes(nonce, sizeof(nonce)) && vault::derive("TestMasterPassword123", salt, 100000, key) && vault::seal(key, nonce, nullptr, 0, message, length, cipher, tag);

    const bool roundtrip_ok = encrypted && vault::open(key, nonce, nullptr, 0, cipher, length, tag, recovered) && memcmp(message, recovered, length) == 0;
    ESP_LOGI(TAG, "Encryption round trip: %s", roundtrip_ok ? "PASS" : "FAIL");
    bool tamper_ok = false;

    if (encrypted) {
        tag[0] ^= 1;

        const bool rejected = !vault::open(
            key, nonce, nullptr, 0,
            cipher, length, tag, recovered
        );

        bool cleared = true;
        for (size_t i = 0; i < length; ++i) {
            if (recovered[i] != 0) {
                cleared = false;
            }
        }
        tamper_ok = rejected && cleared;
    }

    ESP_LOGI(TAG, "Tamper rejection: %s", tamper_ok ? "PASS" : "FAIL");

    vault::wipe(key, sizeof(key));
    vault::wipe(recovered, sizeof(recovered));
}

extern "C" void app_main() {
    ESP_LOGI(TAG, "ESP32 Offline Vault self-test");
    vTaskPrioritySet(nullptr, tskIDLE_PRIORITY);
    run_tests();
    EspFlash flash;
    if (!flash.available()) {
        ESP_LOGE(TAG, "Vault partition not found");
    } 
    else {
        vault::Vault manager(flash);
        ESP_LOGI(TAG, "Vault partition: %lu bytes", static_cast<unsigned long>(flash.size()));
        ESP_LOGI(TAG, "Vault capacity: %lu record slots", static_cast<unsigned long>(manager.capacity()));
        run_admin(manager);
    }

    vTaskDelete(nullptr);
}