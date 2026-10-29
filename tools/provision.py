"""Non-echoing USB provisioning for the firmware's prompt protocol."""
import argparse
import getpass
import time

import serial


def ascii_field(value, maximum, label, minimum=0):
    if not minimum <= len(value) <= maximum or any(not 32 <= ord(c) <= 126 for c in value):
        raise ValueError(f"{label}: use {minimum}..{maximum} printable ASCII characters")
    return value


class Device:
    def __init__(self, port):
        self.serial = serial.Serial(port, 115200, timeout=0.1, write_timeout=2)

    def until(self, marker):
        received = bytearray()
        deadline = time.monotonic() + 90  # Includes PBKDF2 on device.
        while time.monotonic() < deadline:
            received.extend(self.serial.read(1))
            if received.endswith(marker):
                return bytes(received)
            if len(received) > 8192:
                raise RuntimeError("Unexpected device response")
        raise RuntimeError("Device timed out; reconnect and check its locked state")

    def line(self, value):
        self.serial.write(value.encode("ascii") + b"\n")
        self.serial.flush()

    def done(self):
        response = self.until(b"> ")
        if b"OK" not in response:
            raise RuntimeError("Device rejected operation: " + response.decode("ascii", errors="replace"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="USB Serial/JTAG port, e.g. COM7")
    parser.add_argument("action", choices=["init", "add", "time"])
    args = parser.parse_args()
    master = ascii_field(getpass.getpass("Master password: "), 127, "Master password", 12)
    fields = []
    if args.action == "init":
        if master != getpass.getpass("Confirm master password: "):
            raise ValueError("Passwords do not match")
    elif args.action == "add":
        fields = [ascii_field(input("Account name: "), 23, "Name", 1),
                  ascii_field(input("Username: "), 39, "Username"),
                  ascii_field(getpass.getpass("Account password: "), 63, "Password"),
                  ascii_field(getpass.getpass("Base32 TOTP secret (blank to skip): "), 111, "Secret")]
    device = Device(args.port)
    try:
        device.line("")
        device.until(b"> ")
        device.line("init" if args.action == "init" else "unlock")
        device.until(b"Master password: ")
        device.line(master)
        if args.action == "init":
            device.until(b"Confirm master password: ")
            device.line(master)
        device.done()
        if args.action == "add":
            device.line("add")
            for prompt, value in zip([b"Account name: ", b"Username: ", b"Account password: ",
                                      b"Base32 TOTP secret (blank to skip): "], fields):
                device.until(prompt)
                device.line(value)
            device.done()
        elif args.action == "time":
            device.line("time")
            device.until(b"UTC Unix seconds (2000-2099): ")
            device.line(str(int(time.time())))
            device.done()
        print("Operation completed.")
    finally:
        try:
            device.line("lock")
        finally:
            device.serial.close()


if __name__ == "__main__":
    main()
