#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "platform/flash_esp.h"
#include "usb/admin.h"
#include "diagnostics/self_test.h"

extern "C" void app_main() {
    // UI framebuffer lives in internal static RAM, not the task stack or PSRAM.
    static EspFlash flash;
    if (!flash.available()) {
        ESP_LOGE("vault", "Vault partition missing");
        vTaskDelete(nullptr);
        return;
    }
    static vault::Vault manager(flash);
    static UI ui(manager);
    vTaskPrioritySet(nullptr, tskIDLE_PRIORITY);
    const esp_err_t err = ui.begin();
    if (err != ESP_OK) ESP_LOGE("vault", "Display/buttons unavailable: %s", esp_err_to_name(err));
#ifdef CONFIG_VAULT_SELF_TEST
    if (!run_crypto_self_tests()) {
        ui.lock();
        ESP_LOGE("vault", "Crypto self-test failed; app stopped");
        vTaskDelete(nullptr);
        return;
    }
#endif
    run_admin(manager, ui);
}
