# Verification and hardware acceptance

## Recorded software verification — 2026-10-25

| Check | Result |
| --- | --- |
| Production C++ core/RTC/display tests, simulated peripherals | 20 passed |
| USB provisioning helper, scripted serial responses | 5 passed |
| Actual ESP-IDF 6.1.0 ESP32-S3 build, normal configuration | Passed; image 351,200 bytes (0x55be0), 77% of app partition free |
| Optional target crypto self-test configuration | Compiled and linked; not executed on a board |
| Physical display, buttons, USB, RTC, power, enclosure | Not tested; board required |

The installed SDK emitted upstream Kconfig/default-value and component-dependency notices during configuration; application compilation, linking, and partition checks passed. The final image has target self-tests disabled. No board was flashed during software verification.

## Host software tests

On October 28, the enclosure preview renderer was executed and the resulting assembled, exploded, and component PNGs were visually inspected. Input STEP/STL hashes were checked against the preserved hardware snapshot. This is documentation/mesh-render verification only; no KiCad board, SolidWorks import, physical fit, or electronics hardware was tested.

Install [tools/requirements.txt](../tools/requirements.txt). Windows needs native LLVM with lld or a portable Zig compiler; Espressif's esp-clang contains embedded targets only. Reproduce the workspace-local compiler setup:

```powershell
python -m pip install -r tools/requirements.txt
python -m pip install --target build-host/deps ziglang==0.14.1
python tests/run_tests.py --compiler build-host/deps/ziglang/zig.exe
python -m unittest discover -s tests -p test_provision.py -v
```

On Linux with a native compiler: `python tests/run_tests.py --compiler clang++`.

The separate helper tests exercise initialization, account provisioning, UTC time setting, locking, input validation, and authentication rejection with scripted serial responses. They do not verify a physical USB connection or execute the firmware's USB driver.

The runner compiles the production [vault](../core/vault.cpp), [TOTP](../core/totp.cpp), [session](../core/session.cpp), [RTC](../main/drivers/rtc.cpp), and [display](../main/drivers/display.cpp) code. [tests/bridge.cpp](../tests/bridge.cpp) provides NOR-like flash/I2C/SPI simulations and host cryptography callbacks. These are **software tests**. They do not execute ESP-IDF PSA crypto or check electrical behavior.

Coverage: initialization/refusal to overwrite, wrong passwords/retry delay, locked reads/output wiping, tamper detection, interrupted commits, full flash, deleted-account browsing and wraparound, reveal expiry independent of other activity, idle locking, full master entry/unlock with button events, editor limits/clearing, bounce/hold behavior, RFC 6238 SHA-1/256/512 vectors, Base32 validation, RTC date conversion/12-hour mode, invalid BCD/oscillator-stop rejection, persistent update-journal recovery, display address window, framebuffer wiping after transfer, and backlight shutdown on transfer errors. Display simulations check software transactions, not physical panel alignment or SPI signaling.

Separately run the actual firmware build in an activated ESP-IDF 6.1 terminal:

```powershell
idf.py -B build-idf build
```

Compilation, linking, and partition checks cannot verify hardware operation.

## Optional target crypto tests — board required

The original dummy-credential tests are retained in [main/diagnostics/self_test.cpp](../main/diagnostics/self_test.cpp). Enable **Offline Vault → Run dummy-credential crypto self-tests at boot** in `idf.py -B build-idf menuconfig`, build, and flash. Capture UART0 logs at 115200 baud on Feather TX/GND using a 3.3 V USB-UART adapter if needed. Expected results: `TOTP: PASS`, `Encryption round trip: PASS`, `Tamper rejection: PASS`. Failure stops the application. These tests do not modify vault flash; disable the option after qualification.

The target tests have not been executed on hardware in this session.

## Board acceptance checklist — not yet performed

Use dummy credentials. Record board/display/RTC revisions, wiring, firmware revision, expected/observed result, and date.

1. **Boot/display:** inspect wiring, then power over USB. Check no short/heating, native USB enumeration, readable locked screen, full-edge coverage, and orientation. Reset and power-cycle must start locked.
2. **Buttons:** verify each independently. Bouncing taps produce one event. A multi-second hold produces one hold with no subsequent tap. Check enclosure labels; a continuously held button must not keep the vault unlocked indefinitely.
3. **Provisioning:** initialize with a dummy master containing case, punctuation, and spaces. Reject mismatched confirmation, short master, and overlong lines. Repeat `init` to verify data is retained. Add two accounts (with/without TOTP), confirming no input echo or secret logs.
4. **Local unlock:** disconnect USB and unlock with the buttons. Wrong passwords fail, clear entry, and delay retry. Test backspace, held BACK clear, and editor capacity using disposable data.
5. **Browse/reveal:** navigate both ways and wrap. Check long fields and exact password case/punctuation. Test ten-second expiry, browsing during reveal, BACK during reveal, and USB lock during reveal. Password pixels must be replaced and no secret returned over USB.
6. **Idle/cancellation:** wait 60 seconds while unlocked, revealing, and partially entering a master. Confirm lock/clear. TOTP redraws must not extend idle time. Lock with BACK during a pending USB account-password/seed prompt; input must be cancelled without adding an account. Repeat with timeout and overlong input.
7. **RTC/TOTP:** set UTC from a synchronized host; compare codes/countdown with an independent authenticator over multiple boundaries. Verify coin-cell retention after main power loss. Remove backup and main power, then confirm oscillator-stop/invalid time suppresses TOTP until set. Test missing RTC at boot and I2C failure during use: no stale code beyond the next refresh. After hot reconnection, reboot before setting time.
8. **Persistence:** provision, reset, and unlock; data must persist. Delete an account and confirm it disappears locally. Normal firmware update must preserve accounts. Use a disposable vault for controlled interrupted-write tests; partial records must not be exposed and corruption must fail closed. Never erase the only copy of real credentials.
9. **USB/time:** verify helper plus manual CR/LF/CRLF input. `add`, `list`, `delete`, and `time` must reject locked access. Reject malformed/out-of-range timestamps and verify readback. Disconnect the host mid-command and confirm idle locking.
10. **Battery/enclosure:** verify polarity, charging, power switchover, EN-switch behavior, and RTC backup. Measure temperature and runtime. Check pouch clearance, screws/wires, button travel, display window, and USB opening. Existing CAD validation is not a fit test.

## Security review boundaries

Host tests do not establish ESP32 entropy quality, PSA cleanup, USB ring-buffer erasure, compiler/library/stack copies, reset/crash LCD persistence, or flash/debug attack resistance. Explicit wipes cover application-owned buffers. Hardware qualification and a separate security audit remain outstanding before important credentials are stored.
