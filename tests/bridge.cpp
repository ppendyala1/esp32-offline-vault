// Host adapter only: compile production core + RTC without an ESP32 or a CRT.
// Python supplies real cryptography through a callback; I2C and NOR flash are simulated.
#include "../core/session.h"
#include "../core/totp.h"
#include "../main/drivers/rtc.h"
#include "../main/drivers/display.h"
#include "../main/platform/board.h"
#include <string.h>

#ifdef _WIN32
#define API extern "C" __declspec(dllexport)
#else
#define API extern "C" __attribute__((visibility("default")))
#endif

extern "C" {
void* memcpy(void* d, const void* s, size_t n) {
    auto out = static_cast<unsigned char*>(d);
    auto in = static_cast<const unsigned char*>(s);
    for (size_t i = 0; i < n; ++i) out[i] = in[i];
    return d;
}
void* memset(void* d, int c, size_t n) {
    auto out = static_cast<unsigned char*>(d);
    for (size_t i = 0; i < n; ++i) out[i] = c;
    return d;
}
void* memchr(const void* p, int c, size_t n) {
    auto in = static_cast<const unsigned char*>(p);
    for (size_t i = 0; i < n; ++i) if (in[i] == c) return const_cast<unsigned char*>(in + i);
    return nullptr;
}
int memcmp(const void* a, const void* b, size_t n) {
    auto x = static_cast<const unsigned char*>(a), y = static_cast<const unsigned char*>(b);
    for (size_t i = 0; i < n; ++i) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}
size_t strlen(const char* p) { size_t n = 0; while (p[n]) ++n; return n; }
int _purecall() { return 0; }
void __cxa_pure_virtual() {}
#ifdef _WIN32
int _DllMainCRTStartup(void*, unsigned, void*) { return 1; }
#endif
}
void operator delete(void*) noexcept {}
void operator delete(void*, size_t) noexcept {}

using Crypto = int (*)(int, const void**, const size_t*);
static Crypto crypto;
namespace vault {
void wipe(void* p, size_t n) { auto b = static_cast<volatile uint8_t*>(p); while (n--) *b++ = 0; }
bool random_bytes(uint8_t* out, size_t n) {
    const void* a[] = {out}; const size_t sizes[] = {n}; return crypto(0,a,sizes);
}
bool hmac(Hash alg, const uint8_t* key, size_t kn, const uint8_t* msg, size_t mn, uint8_t* out) {
    const void* a[] = {key,msg,out}; const size_t sizes[] = {size_t(alg),kn,mn}; return crypto(1,a,sizes);
}
bool derive(const char* pw, const uint8_t salt[16], uint32_t count, uint8_t key[32]) {
    const void* a[] = {pw,salt,key}; const size_t sizes[] = {count}; return crypto(2,a,sizes);
}
bool seal(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* aad, size_t an,
          const uint8_t* plain, size_t n, uint8_t* cipher, uint8_t tag[16]) {
    const void* a[] = {key,nonce,aad,plain,cipher,tag}; const size_t sizes[] = {an,n}; return crypto(3,a,sizes);
}
bool open(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* aad, size_t an,
          const uint8_t* cipher, size_t n, const uint8_t tag[16], uint8_t* plain) {
    const void* a[] = {key,nonce,aad,cipher,plain,tag}; const size_t sizes[] = {an,n}; return crypto(4,a,sizes);
}
}

class MemoryFlash : public vault::Flash {
public:
    uint8_t bytes[8192];
    int writes = -1;
    uint32_t size() const override { return sizeof(bytes); }
    bool read(uint32_t off, void* out, size_t n) override {
        if (off > size() || n > size() - off) return false;
        memcpy(out, bytes + off, n); return true;
    }
    bool write(uint32_t off, const void* p, size_t n) override {
        if (off > size() || n > size() - off || writes == 0) return false;
        if (writes > 0) --writes;
        const auto in = static_cast<const uint8_t*>(p);
        for (size_t i=0; i<n; ++i) if ((bytes[off+i] & in[i]) != in[i]) return false;
        for (size_t i=0; i<n; ++i) bytes[off+i] &= in[i];
        return true;
    }
    bool erase(uint32_t off, size_t n) override {
        if (off % 4096 || n % 4096 || off > size() || n > size()-off) return false;
        memset(bytes+off,255,n); return true;
    }
};
// Function statics have trivial guards with -fno-threadsafe-statics.
static MemoryFlash& flash() { static MemoryFlash value; return value; }
static vault::Vault& manager() { static vault::Vault value(flash()); return value; }
static vault::Session& session() { static vault::Session value(manager()); return value; }
static vault::Button buttons[4];

API void reset(Crypto callback) {
    crypto = callback;
    session().lock(); session().retry_at = 0; session().last_activity = 0;
    flash().writes = -1; memset(flash().bytes,255,flash().size());
    for (auto& b : buttons) b = vault::Button{};
}
API int auth(const char* pw, int create, uint64_t now) { return int(session().authenticate(pw,create,now)); }
API int add(const vault::Credential* c, uint32_t* id) { return int(manager().add(*c,*id)); }
API int get(uint32_t id, vault::Credential* c) { return int(manager().get(id,*c)); }
API int remove_account(uint32_t id) { return int(manager().remove(id)); }
API int next_account(uint32_t after, uint32_t* id) { return int(manager().next(after,*id)); }
API void lock_session() { session().lock(); }
API void tick(uint64_t now) { session().tick(now); }
API void activity(uint64_t now) { session().activity(now); }
API void button(unsigned index, int event, uint64_t now) { session().button(index,vault::Press(event),now); }
API int debounce(unsigned index, int down, uint64_t now) { return int(buttons[index].update(down,now)); }
API uint64_t state(int which) {
    const auto& s = session();
    switch (which) {
    case 0: return s.manager.unlocked(); case 1: return s.selected; case 2: return s.revealing;
    case 3: return s.length; case 4: return s.character; case 5: return s.retry_at;
    default: return 0;
    }
}
API int entry_cleared() { for (auto c : session().entry) if (c) return 0; return 1; }
API void flash_failure(int writes) { flash().writes = writes; }
API void tamper(uint32_t offset) { if (offset < flash().size()) flash().bytes[offset] ^= 1; }
API int base32(const char* text, uint8_t* out, size_t n, size_t* written) { return vault::decode_base32(text,out,n,*written); }
API int totp(const uint8_t* secret, size_t n, uint64_t utc, uint32_t* code, uint8_t digits, uint32_t period, uint8_t hash) {
    return vault::generate_totp(secret,n,utc,*code,digits,period,vault::Hash(hash));
}

static uint8_t registers[0x20];
static int i2c_writes = -1;
static int i2c_reads = -1;
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t*, void** p) { *p = registers; return 0; }
esp_err_t i2c_del_master_bus(void*) { return 0; }
esp_err_t i2c_master_bus_add_device(void*, const i2c_device_config_t*, void** p) { *p = registers; return 0; }
esp_err_t i2c_master_bus_rm_device(void*) { return 0; }
esp_err_t i2c_master_probe(void*, uint16_t, int) { return 0; }
esp_err_t i2c_master_transmit_receive(void*, const uint8_t* address, size_t, uint8_t* out, size_t n, int) {
    if (i2c_reads == 0) return 99;
    if (i2c_reads > 0) --i2c_reads;
    memcpy(out,registers+*address,n); return 0;
}
esp_err_t i2c_master_transmit(void*, const uint8_t* data, size_t n, int) {
    if (i2c_writes == 0) return 99;
    if (i2c_writes > 0) --i2c_writes;
    for (size_t i=1; i<n; ++i) {
        const size_t address = data[0]+i-1;
        if (address == 15) // OSF and alarm flags: writing one preserves, zero clears.
            registers[address] = (data[i] & ~0x83) | (registers[address] & data[i] & 0x83);
        else registers[address] = data[i];
    }
    return 0;
}
static DS3231& clock_device() { static DS3231 value; return value; }
API void rtc_reset() {
    static bool started = false;
    if (!started) { clock_device().begin(3,4); started = true; }
    memset(registers,0,sizeof(registers)); registers[15]=0x80;
    i2c_writes = i2c_reads = -1;
}
API int rtc_set(uint64_t utc) { return clock_device().set_unix(utc); }
API int rtc_read(uint64_t* utc) { return clock_device().read_unix(*utc); }
API void rtc_register(int address, uint8_t value) { registers[address] = value; }
API void rtc_fail(int writes, int reads) { i2c_writes=writes; i2c_reads=reads; }

static uint8_t panel[240*135*2], column_window[4], row_window[4];
static size_t cursor;
static uint8_t command;
static int levels[49];
static bool spi_failure;
esp_err_t gpio_config(const gpio_config_t*) { return 0; }
esp_err_t gpio_set_level(gpio_num_t pin, int level) { levels[pin]=level; return 0; }
void vTaskDelay(unsigned) {}
esp_err_t spi_bus_initialize(int, const spi_bus_config_t*, int) { return 0; }
esp_err_t spi_bus_add_device(int, const spi_device_interface_config_t*, void** p) { *p=panel; return 0; }
esp_err_t spi_device_polling_transmit(void*, spi_transaction_t* tx) {
    if (spi_failure) return 99;
    if (!levels[board::TFT_DC]) {
        command = *static_cast<const uint8_t*>(tx->tx_buffer);
        if (command == 0x2c) cursor=0;
    } else if (command == 0x2c) {
        const size_t n=tx->length/8;
        if (cursor+n > sizeof(panel)) return 98;
        memcpy(panel+cursor,tx->tx_buffer,n); cursor+=n;
    } else if (command == 0x2a) memcpy(column_window,tx->tx_buffer,4);
    else if (command == 0x2b) memcpy(row_window,tx->tx_buffer,4);
    return 0;
}
static Display& display() { static Display value; return value; }
API int screen_begin() { spi_failure=false; return display().begin(); }
API void screen_text(int x, int y, const char* value, int scale) { display().text(x,y,value,scale); }
API int screen_present() { return display().present(); }
API int screen_nonzero() { int count=0; for (auto b : panel) count += b != 0; return count; }
API int screen_backlight() { return levels[board::TFT_BL]; }
API void screen_fail(int failed) { spi_failure=failed; }
API int screen_window(int which) {
    const uint8_t* window = which < 2 ? column_window : row_window;
    const unsigned offset = (which % 2) * 2;
    return window[offset]*256+window[offset+1];
}

// No process teardown is needed for these static test fixtures.
extern "C" int atexit(void (*)()) { return 0; }
