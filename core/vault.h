#pragma once

#include <stddef.h>
#include <stdint.h>

#include "crypto.h"

namespace vault {

constexpr uint32_t HEADER_SIZE = 4096;
constexpr uint32_t RECORD_SIZE = 256;
constexpr uint32_t KDF_ITERATIONS = 100000;

// Text fields include space for a terminating null character.
// The TOTP secret stores decoded bytes, not Base32 text.
struct Credential {
    char name[24];
    char username[40];
    char password[64];

    uint8_t totp_secret[64];
    uint8_t secret_length;
    uint8_t digits;
    uint8_t period;
    Hash algorithm;
};

static_assert(
    sizeof(Credential) == 196,
    "Credential must occupy 196 bytes"
);

// Implemented later using the ESP32 vault partition.
// Addresses are offsets within that partition.
class Flash {
public:
    virtual ~Flash() = default;

    virtual uint32_t size() const = 0;

    virtual bool read(
        uint32_t offset, void* out, size_t length
    ) = 0;

    virtual bool write(
        uint32_t offset, const void* data, size_t length
    ) = 0;

    virtual bool erase(
        uint32_t offset, size_t length
    ) = 0;
};

enum class Status : uint8_t {
    OK,
    LOCKED,
    INVALID_INPUT,
    NOT_INITIALIZED,
    ALREADY_INITIALIZED,
    AUTH_FAILED,
    NOT_FOUND,
    FULL,
    CORRUPT,
    IO_ERROR,
    CRYPTO_ERROR
};

class Vault {
public:
    explicit Vault(Flash& flash);
    ~Vault();

    Vault(const Vault&) = delete;
    Vault& operator=(const Vault&) = delete;

    Status initialize(const char* master_password);
    Status unlock(const char* master_password);
    void lock();

    bool unlocked() const;

    Status add(const Credential& account, uint32_t& id);
    Status get(uint32_t id, Credential& account);
    Status remove(uint32_t id);

    // Find the next live account after an ID.
    // Use after_id = 0 to start browsing.
    Status next(uint32_t after_id, uint32_t& id);

    uint32_t capacity() const;

private:
    Flash& flash_;
    uint8_t key_[32] = {};
    bool unlocked_ = false;
};

}  // namespace vault