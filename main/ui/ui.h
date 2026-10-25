#pragma once
#include "drivers/display.h"
#include "drivers/rtc.h"
#include "session.h"

class UI {
public:
    explicit UI(vault::Vault& manager) : session(manager) {}
    esp_err_t begin();
    void poll();
    void activity();
    void lock();
    vault::Status authenticate(const char* password, bool create);
    vault::Session session;
    DS3231 rtc;
private:
    void render();
    Display display;
    vault::Button buttons[4];
    uint64_t last_frame = 0;
    bool ready = false;
};
