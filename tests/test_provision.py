"""Host protocol-helper tests using scripted USB responses, without a board."""
import contextlib
import io
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import provision


class Wire:
    def __init__(self, steps):
        self.steps = list(steps)
        self.buffer = bytearray()
        self.closed = False

    def write(self, data):
        expected, response = self.steps.pop(0)
        if data != expected:
            raise AssertionError("Unexpected protocol input")  # Do not log secrets.
        if isinstance(response, Exception):
            raise response
        self.buffer.extend(response)

    def read(self, size):
        data = bytes(self.buffer[:size])
        del self.buffer[:size]
        return data

    def flush(self):
        pass

    def close(self):
        self.closed = True


class ProvisionTests(unittest.TestCase):
    def run_helper(self, action, steps, passwords, fields=()):
        wire, output = Wire(steps), io.StringIO()
        with patch.object(sys, "argv", ["provision.py", "--port", "FAKE", action]), \
             patch.object(provision.serial, "Serial", return_value=wire), \
             patch.object(provision.getpass, "getpass", side_effect=passwords), \
             patch("builtins.input", side_effect=fields), \
             patch.object(provision.time, "time", return_value=1792958400), \
             contextlib.redirect_stdout(output):
            provision.main()
        self.assertTrue(wire.closed)
        self.assertEqual(wire.steps, [])
        for secret in passwords:
            if secret:
                self.assertNotIn(secret, output.getvalue())

    def test_initialize_and_lock(self):
        master = "DummyMasterPassword123!"
        self.run_helper("init", [(b"\n", b"> "), (b"init\n", b"Master password: "),
                        (master.encode()+b"\n", b"Confirm master password: "),
                        (master.encode()+b"\n", b"OK\r\n> "), (b"lock\n", b"Locked.\r\n> ")],
                        [master, master])

    def test_account_add_and_lock(self):
        master, password, seed = "DummyMasterPassword123!", "DummyAccountSecret!", "MY======"
        self.run_helper("add", [(b"\n",b"> "),(b"unlock\n",b"Master password: "),
                        (master.encode()+b"\n",b"OK\r\n> "),(b"add\n",b"Account name: "),
                        (b"Example\n",b"Username: "),(b"user\n",b"Account password: "),
                        (password.encode()+b"\n",b"Base32 TOTP secret (blank to skip): "),
                        (seed.encode()+b"\n",b"OK\r\nAccount ID: 1\r\n> "),(b"lock\n",b"")],
                        [master,password,seed],["Example","user"])

    def test_time_is_utc_unix_seconds_and_locks(self):
        master = "DummyMasterPassword123!"
        self.run_helper("time", [(b"\n",b"> "),(b"unlock\n",b"Master password: "),
                        (master.encode()+b"\n",b"OK\r\n> "),
                        (b"time\n",b"UTC Unix seconds (2000-2099): "),
                        (b"1792958400\n",b"OK time set and read back.\r\n> "),(b"lock\n",b"")], [master])

    def test_input_validation(self):
        for text in ["short", "a"*128, "non-ascii \u00e9", "embedded\nnewline"]:
            with self.assertRaises(ValueError):
                provision.ascii_field(text,127,"Master",12)
        self.assertEqual(provision.ascii_field(" "*12,127,"Master",12), " "*12)

    def test_rejected_authentication_is_reported(self):
        wire = Wire([])
        wire.buffer.extend(b"Authentication failed.\r\n> ")
        with patch.object(provision.serial,"Serial",return_value=wire):
            device = provision.Device("FAKE")
            with self.assertRaisesRegex(RuntimeError,"rejected"):
                device.done()


if __name__ == "__main__":
    unittest.main(verbosity=2)
