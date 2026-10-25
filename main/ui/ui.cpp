#include "ui.h"
#include "platform/board.h"
#include "totp.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

namespace { uint64_t now_ms() { return esp_timer_get_time() / 1000; } }

esp_err_t UI::begin() {
    session.activity(now_ms());
    gpio_config_t config = {};
    for (auto pin : board::BUTTONS) config.pin_bit_mask |= 1ULL << pin;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) return err;
    // Feather's STEMMA QT power gate is active high.
    gpio_set_direction(board::I2C_POWER, GPIO_MODE_OUTPUT);
    gpio_set_level(board::I2C_POWER, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    err = rtc.begin(board::SDA, board::SCL);
    if (err != ESP_OK) ESP_LOGW("ui", "RTC unavailable: %s", esp_err_to_name(err));
    err = display.begin();
    ready = err == ESP_OK;
    if (ready) render();
    return err;
}

void UI::activity() { session.activity(now_ms()); }
void UI::lock() { session.lock(); poll(); }
vault::Status UI::authenticate(const char* password, bool create) {
    auto result = session.authenticate(password, create, now_ms());
    activity(); // KDF/flash work can take seconds.
    poll();
    return result;
}

void UI::poll() {
    uint64_t now = now_ms();
    session.tick(now);
    if (ready) for (unsigned i = 0; i < 4; ++i)
        session.button(i, buttons[i].update(gpio_get_level(board::BUTTONS[i]) == 0, now), now);
    if (ready && (session.dirty || now - last_frame >= 1000)) {
        render(); last_frame = now_ms(); session.dirty = false;
    }
}

void UI::render() {
    display.clear();
    char line[128] = {};
    if (!session.manager.unlocked()) {
        display.text(0, 0, "OFFLINE VAULT", 2);
        display.text(0, 24, "LOCKED - master password");
        snprintf(line, sizeof(line), "%u characters entered", unsigned(session.length));
        display.text(0, 40, line);
        snprintf(line, sizeof(line), "Choose: [%c] ASCII %u", session.character, unsigned(session.character));
        display.text(0, 56, line);
        display.text(0, 80, "UP/DOWN: choose   OK: append\nBACK: erase   hold BACK: clear\nHold OK: unlock   USB: setup");
        const char* message = "";
        if (now_ms() < session.retry_at) message = "Retry in two seconds";
        else if (session.status == vault::Status::NOT_INITIALIZED) message = "Initialize using USB init";
        else if (session.status != vault::Status::OK && session.status != vault::Status::NOT_FOUND)
            message = "Unlock/storage error";
        display.text(0, 120, message);
    } else if (!session.selected) {
        display.text(0, 0, "NO ACCOUNTS", 2);
        display.text(0, 32, "USB add: provision an account\nUP/DOWN: refresh   BACK: lock");
    } else {
        vault::Credential account{};
        if (session.manager.get(session.selected, account) != vault::Status::OK) {
            vault::wipe(&account, sizeof(account));
            session.lock();
            render();
            return;
        }
        session.tick(now_ms()); // A flash scan may have crossed a deadline.
        if (!session.manager.unlocked()) {
            vault::wipe(&account, sizeof(account));
            render();
            return;
        }
        display.text(0, 0, account.name);
        display.text(0, 16, account.username);
        if (session.revealing) {
            display.text(0, 40, account.password); // 40 columns, up to two rows.
            const uint64_t elapsed = now_ms() - session.revealed_at;
            snprintf(line, sizeof(line), "Hidden in %llu seconds", (unsigned long long)
                     ((elapsed < vault::REVEAL_MS ? vault::REVEAL_MS - elapsed : 0) + 999) / 1000);
            display.text(0, 72, line);
        } else display.text(0, 40, "Password hidden - OK to reveal");
        uint64_t utc = 0;
        uint32_t code = 0;
        if (!account.secret_length) snprintf(line, sizeof(line), "No TOTP secret");
        else if (rtc.read_unix(utc) != ESP_OK) snprintf(line, sizeof(line), "RTC invalid - USB time required");
        else if (vault::generate_totp(account.totp_secret, account.secret_length, utc, code,
                                    account.digits, account.period, account.algorithm))
            snprintf(line, sizeof(line), "%0*lu  %lus", account.digits, (unsigned long)code,
                     (unsigned long)(account.period - utc % account.period));
        else snprintf(line, sizeof(line), "TOTP error");
        display.text(0, 88, line, 2);
        display.text(0, 120, "UP/DOWN: browse   BACK: lock");
        vault::wipe(&account, sizeof(account));
        vault::wipe(&code, sizeof(code));
    }
    vault::wipe(line, sizeof(line));
    if (display.present() != ESP_OK) { session.lock(); ready = false; }
}
