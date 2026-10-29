"""Compile and test the production C++ core and RTC using simulated hardware.

Windows: --compiler <zig.exe> needs no Visual Studio; native LLVM also works.
Linux: --compiler clang++ (or g++) uses the native toolchain.
Crypto backend is Python cryptography/hashlib, not the target PSA implementation.
"""
import argparse
import ctypes as C
import hashlib
import hmac
import os
from pathlib import Path
import subprocess
import sys
import unittest

from cryptography.hazmat.primitives.ciphers.aead import AESGCM

ROOT = Path(__file__).resolve().parents[1]
CALLBACK = C.CFUNCTYPE(C.c_int, C.c_int, C.POINTER(C.c_void_p), C.POINTER(C.c_size_t))


@CALLBACK
def crypto(op, args, sizes):
    def read(index, n):
        return C.string_at(args[index], n) if n else b""
    try:
        if op == 0:
            C.memmove(args[0], os.urandom(sizes[0]), sizes[0])
        elif op == 1:
            digest = hmac.new(read(0, sizes[1]), read(1, sizes[2]),
                              {1: "sha1", 2: "sha256", 3: "sha512"}[sizes[0]]).digest()
            C.memmove(args[2], digest, len(digest))
        elif op == 2:
            key = hashlib.pbkdf2_hmac("sha256", C.string_at(args[0]), read(1, 16), sizes[0], 32)
            C.memmove(args[2], key, 32)
        elif op == 3:
            sealed = AESGCM(read(0, 32)).encrypt(read(1, 12), read(3, sizes[1]), read(2, sizes[0]))
            C.memmove(args[4], sealed[:-16], sizes[1])
            C.memmove(args[5], sealed[-16:], 16)
        elif op == 4:
            plain = AESGCM(read(0, 32)).decrypt(read(1, 12), read(3, sizes[1]) + read(5, 16), read(2, sizes[0]))
            C.memmove(args[4], plain, sizes[1])
        return 1
    except Exception:
        if op == 4:
            C.memset(args[4], 0, sizes[1])
        return 0


class Credential(C.Structure):
    _fields_ = [("name", C.c_char * 24), ("username", C.c_char * 40),
                ("password", C.c_char * 64), ("secret", C.c_uint8 * 64),
                ("secret_length", C.c_uint8), ("digits", C.c_uint8),
                ("period", C.c_uint8), ("algorithm", C.c_uint8)]


def compile_library(compiler):
    output = ROOT / "build-host"
    output.mkdir(exist_ok=True)
    sources = [ROOT / f for f in ["tests/bridge.cpp", "core/vault.cpp", "core/totp.cpp",
                                  "core/session.cpp", "main/drivers/rtc.cpp", "main/drivers/display.cpp"]]
    common = ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-fno-builtin", "-fno-exceptions",
              "-fno-rtti", "-fno-threadsafe-statics", "-I" + str(ROOT / "tests/shims"),
              "-I" + str(ROOT / "main"), "-I" + str(ROOT / "core")]
    if sys.platform == "win32":
        library = output / "vault_tests.dll"
        zig = Path(compiler).stem == "zig"
        compiler_command = [compiler, "c++"] if zig else [compiler]
        target = "x86_64-windows-gnu" if zig else "x86_64-windows-msvc"
        if zig:
            os.environ["ZIG_GLOBAL_CACHE_DIR"] = str(output / "zig-cache")
            os.environ["ZIG_LOCAL_CACHE_DIR"] = str(output / "zig-local")
        objects = []
        for source in sources:
            obj = output / (source.stem + ".obj")
            subprocess.run([*compiler_command, "--target=" + target, "-ffreestanding", "-fno-use-cxa-atexit",
                            "-fno-stack-protector", "-nostdinc++", *common,
                            "-c", str(source), "-o", str(obj)], check=True)
            objects.append(str(obj))
        if zig:
            subprocess.run([compiler, "build-lib", "-dynamic", "-target", target,
                            "-fno-entry", "-femit-bin=" + str(library), *objects], check=True)
        else:
            linker = Path(compiler).resolve().with_name("lld.exe")
            subprocess.run([str(linker), "-flavor", "link", "/dll", "/noentry", "/nodefaultlib",
                            "/out:" + str(library), *objects], check=True)
    else:
        library = output / "libvault_tests.so"
        subprocess.run([compiler, *common, "-shared", "-fPIC", *map(str, sources),
                        "-o", str(library)], check=True)
    return C.CDLL(str(library))


def configure(lib):
    u64, u32, ptr = C.c_uint64, C.c_uint32, C.POINTER
    signatures = {
        "reset": ([CALLBACK], None), "auth": ([C.c_char_p, C.c_int, u64], C.c_int),
        "add": ([ptr(Credential), ptr(u32)], C.c_int), "get": ([u32, ptr(Credential)], C.c_int),
        "remove_account": ([u32], C.c_int), "next_account": ([u32, ptr(u32)], C.c_int),
        "lock_session": ([], None), "tick": ([u64], None),
        "activity": ([u64], None),
        "button": ([C.c_uint, C.c_int, u64], None), "state": ([C.c_int], u64),
        "debounce": ([C.c_uint, C.c_int, u64], C.c_int), "entry_cleared": ([], C.c_int),
        "flash_failure": ([C.c_int], None), "tamper": ([u32], None),
        "base32": ([C.c_char_p, C.c_void_p, C.c_size_t, ptr(C.c_size_t)], C.c_int),
        "totp": ([C.c_char_p, C.c_size_t, u64, ptr(u32), C.c_uint8, u32, C.c_uint8], C.c_int),
        "rtc_reset": ([], None), "rtc_set": ([u64], C.c_int),
        "rtc_read": ([ptr(u64)], C.c_int), "rtc_register": ([C.c_int, C.c_uint8], None),
        "rtc_fail": ([C.c_int, C.c_int], None),
        "screen_begin": ([], C.c_int), "screen_present": ([], C.c_int),
        "screen_text": ([C.c_int, C.c_int, C.c_char_p, C.c_int], None),
        "screen_nonzero": ([], C.c_int), "screen_backlight": ([], C.c_int),
        "screen_fail": ([C.c_int], None), "screen_window": ([C.c_int], C.c_int),
    }
    for name, (args, result) in signatures.items():
        getattr(lib, name).argtypes = args
        getattr(lib, name).restype = result


MASTER = b"DummyMasterPassword123!"
OK, LOCKED, INVALID, NOT_INITIALIZED, ALREADY, AUTH_FAILED, NOT_FOUND, FULL, CORRUPT, IO = range(10)


class VaultTests(unittest.TestCase):
    def setUp(self):
        lib.reset(crypto)

    def initialize(self):
        self.assertEqual(lib.auth(MASTER, 1, 0), OK)

    def add(self, name=b"Example"):
        account = Credential(name=name, username=b"test@example.com", password=b"DummyPassword!1")
        identifier = C.c_uint32()
        self.assertEqual(lib.add(C.byref(account), C.byref(identifier)), OK)
        return identifier.value

    def test_initialize_lock_authenticate_and_get(self):
        self.assertEqual(lib.auth(MASTER, 0, 0), NOT_INITIALIZED)
        self.assertEqual(lib.auth(b"short", 1, 2000), INVALID)
        self.assertEqual(lib.auth(MASTER, 1, 4000), OK)
        identifier = self.add()
        lib.lock_session()
        account = Credential(password=b"old sensitive data")
        self.assertEqual(lib.get(identifier, C.byref(account)), LOCKED)
        self.assertEqual(bytes(account), bytes(196))
        self.assertEqual(lib.auth(b"wrong password", 0, 5000), AUTH_FAILED)
        self.assertEqual(lib.auth(MASTER, 0, 5001), AUTH_FAILED)
        self.assertEqual(lib.auth(MASTER, 0, 7000), OK)
        self.assertEqual(lib.get(identifier, C.byref(account)), OK)
        self.assertEqual(account.password, b"DummyPassword!1")
        self.assertEqual(lib.auth(MASTER, 1, 8000), ALREADY)
        self.assertEqual(lib.auth(MASTER, 0, 10000), OK)

    def test_delete_and_browse_wrap(self):
        self.initialize()
        first, second, third = self.add(b"A"), self.add(b"B"), self.add(b"C")
        self.assertEqual(lib.remove_account(second), OK)
        lib.tick(0)
        self.assertEqual(lib.state(1), first)
        lib.button(0, 1, 100)
        self.assertEqual(lib.state(1), third)
        lib.button(1, 1, 200)
        self.assertEqual(lib.state(1), first)
        lib.button(1, 1, 300)
        self.assertEqual(lib.state(1), third)
        self.assertEqual(lib.get(second, C.byref(Credential())), NOT_FOUND)

    def test_tamper_fails_closed_and_wipes_output(self):
        self.initialize()
        identifier = self.add()
        lib.tamper(4096 + 24)
        account = Credential(password=b"old")
        self.assertEqual(lib.get(identifier, C.byref(account)), CORRUPT)
        self.assertEqual(bytes(account), bytes(196))
        self.assertEqual(lib.auth(MASTER, 0, 0), CORRUPT)
        self.assertEqual(lib.state(0), 0)

    def test_interrupted_commit_is_not_exposed(self):
        self.initialize()
        lib.flash_failure(1)  # Payload written, commit marker fails.
        identifier = C.c_uint32()
        self.assertEqual(lib.add(C.byref(Credential(name=b"Partial")), C.byref(identifier)), IO)
        self.assertEqual(identifier.value, 0)
        lib.flash_failure(-1)
        self.assertEqual(lib.auth(MASTER, 0, 0), OK)
        self.assertEqual(lib.next_account(0, C.byref(identifier)), NOT_FOUND)
        self.assertEqual(self.add(b"After failure"), 1)

    def test_full_flash_and_no_automatic_erasure(self):
        self.initialize()
        for _ in range(16):
            self.add()
        self.assertEqual(lib.add(C.byref(Credential(name=b"Overflow")), C.byref(C.c_uint32())), FULL)
        self.assertEqual(lib.auth(MASTER, 0, 0), OK)

    def test_reveal_expiry_and_navigation_hide(self):
        self.initialize()
        self.add()
        lib.tick(0)
        lib.button(2, 1, 1000)
        self.assertEqual(lib.state(2), 1)
        lib.tick(10999)
        self.assertEqual(lib.state(2), 1)
        lib.tick(11000)
        self.assertEqual(lib.state(2), 0)
        lib.button(2, 1, 12000)
        lib.button(1, 1, 12100)
        self.assertEqual(lib.state(2), 0)

    def test_idle_lock_and_partial_entry_clear(self):
        self.initialize()
        lib.tick(59999)
        self.assertEqual(lib.state(0), 1)
        lib.tick(60000)
        self.assertEqual(lib.state(0), 0)
        lib.button(2, 1, 61000)
        self.assertEqual(lib.state(3), 1)
        lib.tick(121000)
        self.assertEqual(lib.state(3), 0)
        self.assertEqual(lib.entry_cleared(), 1)

    def test_editor_ascii_bounds_and_capacity(self):
        lib.button(1, 1, 0)
        self.assertEqual(lib.state(4), ord('`'))
        for i in range(160):
            lib.button(2, 1, i)
        self.assertEqual(lib.state(3), 127)
        lib.button(3, 1, 200)
        self.assertEqual(lib.state(3), 126)
        lib.button(3, 2, 300)
        self.assertEqual(lib.entry_cleared(), 1)

    def test_debounce_bounce_short_and_long_no_repeat(self):
        for down, now in [(1,0),(0,5),(1,10)]:
            self.assertEqual(lib.debounce(0,down,now), 0)
        self.assertEqual(lib.debounce(0,1,40), 0)
        self.assertEqual(lib.debounce(0,0,100), 0)
        self.assertEqual(lib.debounce(0,0,130), 1)
        self.assertEqual(lib.debounce(0,0,500), 0)
        self.assertEqual(lib.debounce(1,1,1000), 0)
        self.assertEqual(lib.debounce(1,1,1030), 0)
        self.assertEqual(lib.debounce(1,1,1730), 2)
        self.assertEqual(lib.debounce(1,1,5000), 0)
        self.assertEqual(lib.debounce(1,0,6000), 0)
        self.assertEqual(lib.debounce(1,0,6030), 0)

    def test_master_entry_unlock_with_buttons(self):
        self.initialize()
        identifier = self.add()
        lib.lock_session()
        now = 0
        for character in MASTER:
            distance = (character - lib.state(4)) % 95
            for _ in range(distance):
                lib.button(0, 1, now)
                now += 1
            lib.button(2, 1, now)
            now += 1
        self.assertEqual(lib.state(3), len(MASTER))
        lib.button(2, 2, now)
        lib.tick(now + 1)
        self.assertEqual(lib.state(0), 1)
        self.assertEqual(lib.state(1), identifier)
        self.assertEqual(lib.entry_cleared(), 1)

    def test_activity_does_not_extend_password_reveal(self):
        self.initialize()
        self.add()
        lib.tick(0)
        lib.button(2, 1, 1000)
        lib.activity(10000)
        lib.tick(11000)
        self.assertEqual(lib.state(2), 0)
        lib.tick(69999)
        self.assertEqual(lib.state(0), 1)
        lib.tick(70000)
        self.assertEqual(lib.state(0), 0)


class TotpTests(unittest.TestCase):
    def test_rfc6238_all_sha_variants(self):
        times = [59,1111111109,1111111111,1234567890,2000000000,20000000000]
        expected = [[94287082,7081804,14050471,89005924,69279037,65353130],
                    [46119246,68084774,67062674,91819424,90698825,77737706],
                    [90693936,25091201,99943326,93441116,38618901,47863826]]
        for algorithm, length in [(1,20),(2,32),(3,64)]:
            secret = (b"1234567890" * 7)[:length]
            for utc, expected_code in zip(times, expected[algorithm-1]):
                code = C.c_uint32()
                self.assertEqual(lib.totp(secret,len(secret),utc,C.byref(code),8,30,algorithm),1)
                self.assertEqual(code.value,expected_code)

    def test_base32_canonical_and_invalid_wipe(self):
        for text, expected in [(b"MY======",b"f"),(b"mzxw6",b"foo"),(b"MZXW6YTB",b"fooba")]:
            out, count = C.create_string_buffer(64), C.c_size_t()
            self.assertEqual(lib.base32(text,out,64,C.byref(count)),1)
            self.assertEqual(out.raw[:count.value],expected)
        for text in [b"",b"A",b"MZ",b"MY=",b"MY======A",b"MZX!",b"MZXW6YTB"]:
            out, count = C.create_string_buffer(4), C.c_size_t()
            self.assertEqual(lib.base32(text,out,4,C.byref(count)),0)
            self.assertEqual(count.value,0)
            self.assertEqual(out.raw,bytes(4))


class RtcTests(unittest.TestCase):
    def setUp(self):
        lib.rtc_reset()

    def read(self):
        value = C.c_uint64(999)
        status = lib.rtc_read(C.byref(value))
        return status,value.value

    def test_osf_and_calendar_roundtrip(self):
        self.assertNotEqual(self.read()[0],0)
        for value in [946684800,951782400,1709251199,1792958400,4102444799]:
            self.assertEqual(lib.rtc_set(value),0)
            self.assertEqual(self.read(),(0,value))
        self.assertNotEqual(lib.rtc_set(946684799),0)
        self.assertNotEqual(lib.rtc_set(4102444800),0)

    def test_12_hour_and_invalid_registers(self):
        self.assertEqual(lib.rtc_set(946684800),0)
        lib.rtc_register(2,0x72)  # 12 PM
        self.assertEqual(self.read(),(0,946684800+43200))
        lib.rtc_register(2,0x52)  # 12 AM
        self.assertEqual(self.read(),(0,946684800))
        for address, value in [(0,0x6a),(1,0x60),(2,0x24),(3,0),(4,0),(5,0x81),(15,0x80)]:
            lib.rtc_set(946684800)
            lib.rtc_register(address,value)
            self.assertNotEqual(self.read()[0],0)
            self.assertEqual(self.read()[1],0)

    def test_failed_time_update_stays_invalid(self):
        for successful_writes in [1,2,3]:
            lib.rtc_reset()
            lib.rtc_set(946684800)
            lib.rtc_fail(successful_writes,-1)
            self.assertNotEqual(lib.rtc_set(1709251199),0)
            lib.rtc_fail(-1,-1)
            self.assertNotEqual(self.read()[0],0)
        lib.rtc_fail(-1,0)
        self.assertNotEqual(self.read()[0],0)

    def test_missing_or_dirty_update_journal_rejected(self):
        lib.rtc_set(946684800)
        for marker in [0, 0x80, 0xff]:
            lib.rtc_register(0x0b, marker)
            self.assertNotEqual(self.read()[0], 0)
        self.assertEqual(lib.rtc_set(1792958400), 0)
        self.assertEqual(self.read(), (0,1792958400))


class DisplayTests(unittest.TestCase):
    def setUp(self):
        self.assertEqual(lib.screen_begin(), 0)

    def test_window_and_full_frame_transfer(self):
        self.assertEqual([lib.screen_window(i) for i in range(4)], [40,279,53,187])
        self.assertEqual(lib.screen_nonzero(), 0)
        lib.screen_text(0,0,b"DummyPassword! Aa09",1)
        self.assertEqual(lib.screen_present(), 0)
        self.assertGreater(lib.screen_nonzero(), 0)

    def test_framebuffer_wiped_after_transfer(self):
        lib.screen_text(0,0,b"Sensitive screen data",2)
        lib.screen_present()
        self.assertGreater(lib.screen_nonzero(), 0)
        # A second transfer without drawing must contain zeroed pixels only.
        lib.screen_present()
        self.assertEqual(lib.screen_nonzero(), 0)

    def test_transfer_failure_hides_backlight(self):
        self.assertEqual(lib.screen_backlight(), 1)
        lib.screen_fail(1)
        self.assertNotEqual(lib.screen_present(), 0)
        self.assertEqual(lib.screen_backlight(), 0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="clang++")
    args = parser.parse_args()
    lib = compile_library(args.compiler)
    configure(lib)
    lib.reset(crypto)
    unittest.main(argv=[sys.argv[0]], verbosity=2)
