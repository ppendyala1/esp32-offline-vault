# Parts and manufacturer documentation

Manufacturer pages checked 2026-10-25. Dimensions below describe purchased components, not fully detailed CAD models. Links are supplied rather than redistributed vendor PDFs.

| Qty | Part | Documentation / purchase | Mechanical note |
|---|---|---|---|
|1|Adafruit Feather ESP32-S3 4 MB / 2 MB, PID 5477|https://www.adafruit.com/product/5477 ; https://learn.adafruit.com/adafruit-esp32-s3-feather/downloads | Mount on tray with USB toward +Y; adjust foam spacers |
|1|Adafruit ST7789 1.14 inch 240x135, PID 4383|https://www.adafruit.com/product/4383 |32 x 27.8 x 5.7 mm; align window physically|
|1|Adafruit DS3231, PID 3013|https://www.adafruit.com/product/3013 ; https://www.analog.com/media/en/technical-documentation/data-sheets/DS3231.pdf |23 x 17.6 x 7.2 mm|
|1|Adafruit LiPo 3.7 V 1200 mAh, PID 258|https://www.adafruit.com/product/258 (Battery Datasheet link under Technical Details)|34 x 62 x 5 mm; inspect actual cell and lead clearance|
|4|Adafruit black 16 mm momentary panel button, PID 1505|https://www.adafruit.com/product/1505 |16.4 mm modeled holes; verify nut/terminal clearance|
|1|CR1220 battery for RTC|RTC product documentation above|Check holder polarity|
|1|SPST panel toggle, 6 mm threaded bushing|Select a supplier part with drawing confirming 6 mm bushing, >=3 mm usable panel thread, <=15 mm internal body depth|6.4 mm prototype hole; exact switch remains to be sourced|
|4|M3 x 10 pan-head screws|Standard metric machine fasteners|Pre-tap printed pilot holes; no heat-set inserts assumed|
|1 set|USB-C data cable, 26–28 AWG insulated hookup wire, heat shrink, removable electronics foam tape, insulating spacers|Local electronics supplier|Avoid bulky header stacks|

The panel buttons replace the loose breadboard tactile switches from the earlier list. They avoid a custom button PCB and printed plungers. This increases enclosure size but simplifies a first build. The toggle is intentionally a dimensional procurement specification, not a falsely verified exact SKU.

Do not connect two chargers to the same battery. Use the Feather charging path and its matching battery connector polarity. No runtime claim is made.
