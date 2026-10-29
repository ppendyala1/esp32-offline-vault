# Wiring — Feather ESP32-S3 PID 5477

This proposed wiring has not been tested on the board. Numbers are **ESP GPIO numbers**, not header positions. Confirm the 4 MB flash / 2 MB PSRAM Feather and its silkscreen. Firmware assignments: [main/platform/board.h](../main/platform/board.h). Pin mapping is cross-checked against [Adafruit's guide](https://learn.adafruit.com/adafruit-esp32-s3-feather/pinouts) and the [PID 5477 CircuitPython board definition](https://github.com/adafruit/circuitpython/blob/main/ports/espressif/boards/adafruit_feather_esp32s3_4mbflash_2mbpsram/pins.c). Disconnect power before wiring; all signals are 3.3 V.

## Recommended assembly method

Use the existing breakout boards with soldered wiring or a small perfboard for the first unit. First verify the connections on the bench, then check physical fit before soldering the permanent wiring. A custom carrier PCB is optional after the prototype connections and dimensions are verified. No custom PCB design currently exists in this repository.

Connect module pads/header pins and button terminals with insulated wire. Use sockets or detachable lid leads where space permits, insulate joints, and add strain relief. Keep the LiPo on the Feather's battery connector and use its built-in charger. The current enclosure has adhesive module mounts, not a custom-PCB mounting pattern.

## ST7789 1.14-inch TFT, PID 4383

| TFT pin | Feather connection | ESP GPIO |
| --- | --- | --- |
| VIN | 3V | — |
| GND | GND | — |
| SCK | SCK | 36 |
| MOSI | MO | 35 |
| CS | D6 | 6 |
| D/C | D5 | 5 |
| RST | D9 | 9 |
| BL | D10 | 10 |
| Other pins / SD socket | Unconnected | — |

Use short SPI wires and common ground. No MISO is needed. Firmware uses SPI2, mode 0, 20 MHz, RGB565 monochrome text, landscape 240x135, RAM offsets X=40..279/Y=53..187, MADCTL=0x60, and inversion on. Verify coverage/orientation on the actual PID 4383. Other ST7789 panels can need different offsets. Connect BL so firmware can hide a display on SPI failure.

## DS3231, PID 3013

| RTC pin | Feather connection | ESP GPIO |
| --- | --- | --- |
| VIN | 3V | — |
| GND | GND | — |
| SDA | SDA / STEMMA QT SDA | 3 |
| SCL | SCL / STEMMA QT SCL | 4 |
| SQW / 32K | Unconnected | — |

Use the breakout's pull-ups to 3.3 V, never 5 V. I2C address is 0x68 at 100 kHz. Firmware enables the Feather's active-high STEMMA/I2C power gate on GPIO7; do not reuse it. Install the specified CR1220 backup cell with correct polarity. Set UTC over USB before using TOTP, including after upgrading from the earlier RTC code. Alarm 2's minute register is reserved as a persistent time-update journal and its interrupt is disabled; do not configure alarms with another program. The cell backs up only the RTC and is not charged by the Feather. Verify the actual holder/board labeling before inserting it.

## Four buttons

Each normally-open momentary switch connects its GPIO to **GND when pressed**; firmware enables internal pull-ups.

| Button | Feather label | ESP GPIO |
| --- | --- | --- |
| UP | D11 | 11 |
| DOWN | D12 | 12 |
| OK | A3 | 15 |
| BACK | A4 | 14 |

Check continuity on four-leg switches: paired legs can already be connected. Enclosure layout is UP/DOWN across the upper pair and BACK/OK across the lower pair. Wire by label. GPIO13 is reserved for the onboard red LED, whose load is unsuitable for an input relying on an internal pull-up. GPIO19/20 remain reserved for native USB; do not use flash/PSRAM pins or BOOT GPIO0 for navigation. SPI MISO is unused. UART0 application logs are routed to Feather TX/GPIO39 and RX/GPIO38 at 115200 baud; connect a 3.3 V USB-UART adapter's RX to Feather TX and its GND to GND. Do not connect the adapter's power pin.

## LiPo and power

Connect the protected 3.7 V nominal 1200 mAh LiPo to the Feather battery connector only after confirming polarity matches Adafruit's. Use the **built-in Feather charger**, without another charging module. USB-C must be a data cable. An optional slide switch shorts EN to GND to disable the regulator; it does not disconnect the battery and USB charging can remain active while logic is off. Keep the RTC cell separate.

Insulate wiring and keep screws, terminals, and hard edges away from the pouch. Verify battery dimensions/lead bend, USB clearance, switch fit, and button travel against the actual enclosure before final assembly. Runtime, current draw, charging/thermal behavior, switchover, and fit must be measured; no battery-runtime estimate is claimed.
