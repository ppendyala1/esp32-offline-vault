# ESP32 Offline Password Manager + TOTP

This project I'm building a handheld offline password manager and TOTP generator using an ESP32-S3, TFT display, DS3231 RTC, and four buttons.

## Current progress

The desktop demo supports encrypted account storage, password-based unlocking, and 30-second TOTP codes. ESP32 integration and hardware testing are upcoming.

## Run the desktop demo

Requires Python 3.10 or later.

```powershell
python -m pip install -r tools/requirements.txt
python tools/desktop_demo.py
```

Use dummy credentials while testing.
