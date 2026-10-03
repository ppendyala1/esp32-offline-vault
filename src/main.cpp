#include <Arduino.h>

void setup() {
    Serial.begin(115200);
}

void loop() {
    static bool announced = false;

    if (Serial && !announced) {
        Serial.println("ESP32 Offline Vault");
        Serial.println("Firmware started.");
        announced = true;
    }

    delay(10);
}