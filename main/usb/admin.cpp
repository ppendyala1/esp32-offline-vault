#include "admin.h"

#include <stdio.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "totp.h"
namespace {

constexpr int64_t TIMEOUT_US = 60LL * 1000000;

void send(const char* text) {
    usb_serial_jtag_write_bytes(
        text, strlen(text), pdMS_TO_TICKS(100)
    );
}

void report(vault::Status status) {
    switch (status) {
        case vault::Status::OK:
            send("OK\r\n");
            break;
        case vault::Status::AUTH_FAILED:
            send("Authentication failed.\r\n");
            break;
        case vault::Status::NOT_INITIALIZED:
            send("Vault not initialized. Use init.\r\n");
            break;
        case vault::Status::ALREADY_INITIALIZED:
            send("Flash already contains data. Nothing erased.\r\n");
            break;
        case vault::Status::INVALID_INPUT:
            send("Invalid input. New master passwords need 12 characters.\r\n");
            break;
        case vault::Status::CORRUPT:
            send("Vault data is corrupt or authentication failed.\r\n");
            break;
        case vault::Status::IO_ERROR:
            send("Flash operation failed.\r\n");
            break;
        case vault::Status::CRYPTO_ERROR:
            send("Cryptographic operation failed.\r\n");
            break;
        case vault::Status::LOCKED:
            send("Vault locked. Use unlock.\r\n");
            break;
        case vault::Status::NOT_FOUND:
            send("Account not found.\r\n");
            break;
        case vault::Status::FULL:
            send("Vault storage is full.\r\n");
            break;
        default:
            send("Operation failed.\r\n");
            break;
    }
}

// Input is never echoed back by the device.
// Overlong lines are discarded instead of truncated.
bool read_line(UI& ui,
               char* out, size_t capacity,
               int64_t& last_activity,
               bool prompt_timeout) {
    static bool skip_lf = false;
    const uint32_t epoch = ui.session.lock_epoch;

    size_t used = 0;
    bool overflow = false;
    out[0] = '\0';

    while (true) {
        ui.poll();
        if (epoch != ui.session.lock_epoch) {
            vault::wipe(out, capacity);
            send("\r\nLocked; input cancelled.\r\n");
            return false;
        }
        const int64_t now = esp_timer_get_time();

        if (now - last_activity >= TIMEOUT_US) {
            if (prompt_timeout || used != 0 || overflow) {
                vault::wipe(out, capacity);
                ui.lock();
                return false;
            }
        }

        uint8_t c = 0;

        if (usb_serial_jtag_read_bytes(
                &c, 1, pdMS_TO_TICKS(20)) != 1) {
            continue;
        }

        last_activity = esp_timer_get_time();
        ui.activity();

        if (skip_lf) {
            skip_lf = false;
            if (c == '\n') continue;
        }

        if (c == '\r' || c == '\n') {
            skip_lf = c == '\r';
            send("\r\n");

            if (overflow) {
                vault::wipe(out, capacity);
                send("Input too long.\r\n");
                return false;
            }

            out[used] = '\0';
            return true;
        }

        if (c == 8 || c == 127) {
            if (!overflow && used != 0) {
                out[--used] = '\0';
            }
            continue;
        }

        // This initial interface accepts printable ASCII.
        if (c < 32 || c > 126) {
            overflow = true;
            continue;
        }

        if (used + 1 >= capacity) {
            overflow = true;
        } else if (!overflow) {
            out[used++] = static_cast<char>(c);
            out[used] = '\0';
        }
    }
}

}  // namespace

void run_admin(vault::Vault& manager, UI& ui) {
    usb_serial_jtag_driver_config_t config = {};
    config.rx_buffer_size = 512;
    config.tx_buffer_size = 512;

    if (usb_serial_jtag_driver_install(&config) != ESP_OK) {
        ui.lock();
        while (true) { ui.poll(); vTaskDelay(pdMS_TO_TICKS(10)); }
    }

    ui.lock();
    int64_t last_activity = esp_timer_get_time();

    send("\r\nESP32 Offline Vault\r\n");
    send("Commands: init unlock add list delete time lock status help\r\n");

    while (true) {
        char command[24] = {};
        send("> ");

        if (!read_line(ui, command, sizeof(command),
                last_activity, false)) {
            continue;
        }

        if (strcmp(command, "init") == 0 ||
            strcmp(command, "unlock") == 0) {
            const bool creating = strcmp(command, "init") == 0;
            char password[128] = {};
            char confirmation[128] = {};

            send("Master password: ");

            bool ready = read_line(ui, password, sizeof(password),
                last_activity, true
            );

            if (ready && creating) {
                send("Confirm master password: ");

                ready = read_line(ui, confirmation, sizeof(confirmation),
                    last_activity, true
                );

                if (ready &&
                    strcmp(password, confirmation) != 0) {
                    send("Passwords do not match.\r\n");
                    ready = false;
                }
            }
            if (ready) {
                report(ui.authenticate(password, creating));
                last_activity = esp_timer_get_time();
            }

            vault::wipe(password, sizeof(password));
            vault::wipe(confirmation, sizeof(confirmation));
        } 
        else if (strcmp(command, "add") == 0) {
            if (!manager.unlocked()) {
                report(vault::Status::LOCKED);
            } else {
                vault::Credential account{};
                char base32[112] = {};
                bool ready = true;

                send("Account name: ");
                ready = read_line(ui, account.name, sizeof(account.name), last_activity, true);

                if (ready) {
                    send("Username: ");
                    ready = read_line(ui, account.username,
                        sizeof(account.username),
                        last_activity, true
                    );
                }

                if (ready) {
                    send("Account password: ");
                    ready = read_line(ui, account.password, sizeof(account.password), last_activity, true);
                }

                if (ready) {
                    send("Base32 TOTP secret (blank to skip): ");
                    ready = read_line(ui, base32, sizeof(base32), last_activity, true);
                }

                account.digits = 6;
                account.period = 30;
                account.algorithm = vault::Hash::SHA1;

                if (ready && base32[0] != '\0') {
                    size_t written = 0;
                    ready = vault::decode_base32(
                        base32,
                        account.totp_secret,
                        sizeof(account.totp_secret),
                        written
                    );

                    if (ready) {
                        account.secret_length = static_cast<uint8_t>(written);
                    } 
                    else {
                        send("Invalid Base32 secret.\r\n");
                    }
                }
                if (ready) {
                    uint32_t id = 0;
                    const vault::Status status = manager.add(account, id);
                    report(status);
                    if (status == vault::Status::OK) {
                        char line[48];
                        snprintf(line, sizeof(line), "Account ID: %lu\r\n", static_cast<unsigned long>(id));
                        send(line);
                    }
                    last_activity = esp_timer_get_time();
                }
                vault::wipe(base32, sizeof(base32));
                vault::wipe(&account, sizeof(account));
            }
        } 
        else if (strcmp(command, "list") == 0) {
            if (!manager.unlocked()) {
                report(vault::Status::LOCKED);
            } 
            else {
                uint32_t after = 0;
                bool listed = false;

                while (true) {
                    if (esp_timer_get_time() - last_activity >=
                        TIMEOUT_US) {
                        ui.lock();
                        send("Automatically locked.\r\n");
                        break;
                    }

                    uint32_t id = 0;
                    vault::Status status =
                        manager.next(after, id);

                    if (status == vault::Status::NOT_FOUND) {
                        send(listed
                            ? "End of accounts.\r\n"
                            : "No accounts.\r\n");
                        break;
                    }

                    if (status != vault::Status::OK) {
                        report(status);
                        break;
                    }

                    vault::Credential account{};
                    status = manager.get(id, account);

                    // A large scan may take time. Recheck before output.
                    if (esp_timer_get_time() - last_activity >=
                        TIMEOUT_US) {
                        vault::wipe(&account, sizeof(account));
                        ui.lock();
                        send("Automatically locked.\r\n");
                        break;
                    }

                    if (status != vault::Status::OK) {
                        vault::wipe(&account, sizeof(account));
                        report(status);
                        break;
                    }

                    char line[96];
                    snprintf(line, sizeof(line), "%lu | %s | %s\r\n", static_cast<unsigned long>(id), account.name, account.username);

                    send(line);
                    vault::wipe(line, sizeof(line));
                    vault::wipe(&account, sizeof(account));
                    after = id;
                    listed = true;
                    vTaskDelay(pdMS_TO_TICKS(1));
                }
            }
        } 
        else if (strcmp(command, "delete") == 0) {
            if (!manager.unlocked()) {
                report(vault::Status::LOCKED);
            } 
            else {
                char text[16] = {};
                send("Account ID to delete: ");

                if (read_line(ui, text, sizeof(text), last_activity, true)) {
                    uint32_t id = 0;
                    bool valid = text[0] != '\0';

                    for (size_t i = 0; text[i] != '\0'; ++i) {
                        if (text[i] < '0' || text[i] > '9') {
                            valid = false;
                            break;
                        }
                        const uint32_t digit = static_cast<uint32_t>(text[i] - '0');
                        if (id > (UINT32_MAX - digit) / 10) {
                            valid = false;
                            break;
                        }
                        id = id * 10 + digit;
                    }
                    if (!valid || id == 0) {
                        send("Invalid account ID.\r\n");
                    } 
                    else {
                        char confirmation[8] = {};
                        send("Type YES to delete: ");
                        if (read_line(ui, confirmation, sizeof(confirmation), last_activity, true) && strcmp(confirmation, "YES") == 0) {
                            report(manager.remove(id));
                            last_activity = esp_timer_get_time();
                        } 
                        else {
                            send("Deletion cancelled.\r\n");
                        }
                        vault::wipe(confirmation, sizeof(confirmation));
                    }
                }
                vault::wipe(text, sizeof(text));
            }
        }
        else if (strcmp(command, "time") == 0) {
            if (!manager.unlocked()) report(vault::Status::LOCKED);
            else {
                char text[24] = {};
                send("UTC Unix seconds (2000-2099): ");
                if (read_line(ui, text, sizeof(text), last_activity, true)) {
                    uint64_t seconds = 0;
                    bool valid = text[0] != 0;
                    for (size_t i = 0; text[i]; ++i) {
                        const unsigned digit = static_cast<unsigned>(text[i] - '0');
                        if (digit > 9 || seconds > (UINT64_MAX - digit) / 10) { valid = false; break; }
                        seconds = seconds * 10 + digit;
                    }
                    if (!manager.unlocked()) report(vault::Status::LOCKED);
                    else if (!valid) send("Invalid UTC seconds.\r\n");
                    else {
                        uint64_t readback = 0;
                        if (ui.rtc.set_unix(seconds) == ESP_OK &&
                            ui.rtc.read_unix(readback) == ESP_OK &&
                            readback >= seconds && readback - seconds <= 1)
                            send("OK time set and read back.\r\n");
                        else send("RTC update/readback failed.\r\n");
                    }
                }
                vault::wipe(text, sizeof(text));
            }
        }
        else if (strcmp(command, "lock") == 0) {
            ui.lock();
            send("Locked.\r\n");
        } 
        else if (strcmp(command, "status") == 0) {
            send(manager.unlocked() ? "Unlocked.\r\n" : "Locked.\r\n");
        } 
        else if (strcmp(command, "help") == 0) {
            send("Commands: init unlock add list delete time lock status help\r\n");
        } 
        else if (command[0] != '\0') {
            send("Commands: init unlock add list delete time lock status help\r\n");
        }
        vault::wipe(command, sizeof(command));
        ui.session.refresh();
        ui.poll();
    }
}
