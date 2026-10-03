#include "vault.h"
#include <string.h>

namespace vault {
namespace {
constexpr uint32_t COMMIT = 252;
constexpr uint32_t ACCOUNT = 1;
constexpr uint32_t DELETED = 2;
const uint8_t HEADER_MAGIC[8] = {'E','S','P','V','L','T','0','1'};
const uint8_t RECORD_MAGIC[4] = {'R','E','C','1'};
const uint8_t CHECK[16] = {'V','a','u','l','t','K','e','y','C','h','e','c','k','0','0','1'};

uint32_t load32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
void save32(uint8_t* p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(value >> (8 * i));
}
bool erased(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) if (p[i] != 0xFF) return false;
    return true;
}
bool geometry(const Flash& flash) {
    return flash.size() >= HEADER_SIZE + RECORD_SIZE &&
           flash.size() % HEADER_SIZE == 0;
}
bool valid(const Credential& c) {
    if (c.name[0] == '\0' || !memchr(c.name, 0, sizeof(c.name)) ||
        !memchr(c.username, 0, sizeof(c.username)) ||
        !memchr(c.password, 0, sizeof(c.password)) ||
        c.secret_length > sizeof(c.totp_secret)) return false;
    // Password-only entries do not need TOTP settings.
    if (c.secret_length == 0) return true;
    return c.digits >= 6 && c.digits <= 8 && c.period != 0 &&
           (c.algorithm == Hash::SHA1 || c.algorithm == Hash::SHA256 ||
            c.algorithm == Hash::SHA512);
}

// Write the commit marker last. An interrupted write consumes a slot,
// but an uncommitted record is never exposed as an account.
Status commit(Flash& flash, uint32_t offset, const uint8_t bytes[256]) {
    if (!flash.write(offset, bytes, COMMIT)) return Status::IO_ERROR;
    const uint8_t marker[4] = {};
    if (!flash.write(offset + COMMIT, marker, sizeof(marker)))
        return Status::IO_ERROR;
    return Status::OK;
}

struct Scan {
    uint32_t next_slot = 0;
    uint32_t maximum_id = 0;
    uint32_t candidate = 0;
    bool found = false;
    bool deleted = false;
};

// Authenticate every committed record, including deletion records.
// A later record for an ID replaces the earlier one.
Status scan(Flash& flash, const uint8_t key[32], uint32_t wanted,
            uint32_t after, Scan& result, Credential* account) {
    result = Scan{};
    const uint32_t slots = (flash.size() - HEADER_SIZE) / RECORD_SIZE;
    uint8_t bytes[RECORD_SIZE];
    uint8_t plain[sizeof(Credential)];
    Status status = Status::OK;
    for (uint32_t slot = 0; slot < slots; ++slot) {
        const uint32_t offset = HEADER_SIZE + slot * RECORD_SIZE;
        if (!flash.read(offset, bytes, sizeof(bytes))) {
            status = Status::IO_ERROR;
            break;
        }
        if (erased(bytes, sizeof(bytes))) continue;
        result.next_slot = slot + 1;
        if (load32(bytes + COMMIT) != 0) continue;
        const uint32_t id = load32(bytes + 4);
        const uint32_t kind = load32(bytes + 8);
        if (memcmp(bytes, RECORD_MAGIC, sizeof(RECORD_MAGIC)) != 0 ||
            id == 0 || (kind != ACCOUNT && kind != DELETED) ||
            !open(key, bytes + 12, bytes, 12, bytes + 24,
                  sizeof(plain), bytes + 220, plain)) {
            status = Status::CORRUPT;
            break;
        }
        Credential decoded{};
        memcpy(&decoded, plain, sizeof(decoded));
        if (kind == ACCOUNT && !valid(decoded)) {
            wipe(&decoded, sizeof(decoded));
            status = Status::CORRUPT;
            break;
        }
        if (id > result.maximum_id) result.maximum_id = id;
        if (id == wanted) {
            result.found = true;
            result.deleted = kind == DELETED;
            if (account) {
                wipe(account, sizeof(*account));
                if (kind == ACCOUNT) memcpy(account, &decoded, sizeof(decoded));
            }
        }
        if (id > after &&
            (result.candidate == 0 || id < result.candidate))
            result.candidate = id;
        wipe(&decoded, sizeof(decoded));
        wipe(plain, sizeof(plain));
    }
    wipe(plain, sizeof(plain));
    wipe(bytes, sizeof(bytes));
    if (status != Status::OK && account) wipe(account, sizeof(*account));
    return status;
}

Status append(Flash& flash, const uint8_t key[32], uint32_t slot,
              uint32_t id, uint32_t kind, const Credential& account) {
    if (slot >= (flash.size() - HEADER_SIZE) / RECORD_SIZE)
        return Status::FULL;
    uint8_t bytes[RECORD_SIZE];
    memset(bytes, 0xFF, sizeof(bytes));
    memcpy(bytes, RECORD_MAGIC, sizeof(RECORD_MAGIC));
    save32(bytes + 4, id);
    save32(bytes + 8, kind);
    Status status = Status::CRYPTO_ERROR;
    if (random_bytes(bytes + 12, 12) &&
        seal(key, bytes + 12, bytes, 12,
             reinterpret_cast<const uint8_t*>(&account), sizeof(account),
             bytes + 24, bytes + 220)) {
        status = commit(flash, HEADER_SIZE + slot * RECORD_SIZE, bytes);
    }
    wipe(bytes, sizeof(bytes));
    return status;
}
} // namespace

Vault::Vault(Flash& flash) : flash_(flash) {}
Vault::~Vault() { lock(); }

void Vault::lock() {
    wipe(key_, sizeof(key_));
    unlocked_ = false;
}
bool Vault::unlocked() const { return unlocked_; }
uint32_t Vault::capacity() const {
    return geometry(flash_) ? (flash_.size() - HEADER_SIZE) / RECORD_SIZE : 0;
}

Status Vault::initialize(const char* master_password) {
    lock();
    if (!master_password || strlen(master_password) < 12 || !geometry(flash_))
        return Status::INVALID_INPUT;
    uint8_t bytes[RECORD_SIZE];
    // Initialization never erases existing data, including partial writes.
    for (uint32_t offset = 0; offset < flash_.size(); offset += RECORD_SIZE) {
        if (!flash_.read(offset, bytes, sizeof(bytes))) return Status::IO_ERROR;
        if (!erased(bytes, sizeof(bytes))) return Status::ALREADY_INITIALIZED;
    }
    memset(bytes, 0xFF, sizeof(bytes));
    memcpy(bytes, HEADER_MAGIC, sizeof(HEADER_MAGIC));
    save32(bytes + 24, KDF_ITERATIONS);
    Status status = Status::CRYPTO_ERROR;
    if (random_bytes(bytes + 8, 16) && random_bytes(bytes + 28, 12) &&
        derive(master_password, bytes + 8, KDF_ITERATIONS, key_) &&
        seal(key_, bytes + 28, bytes, 28, CHECK, sizeof(CHECK),
             bytes + 40, bytes + 56)) {
        status = commit(flash_, 0, bytes);
    }
    wipe(bytes, sizeof(bytes));
    if (status == Status::OK) unlocked_ = true;
    else lock();
    return status;
}

Status Vault::unlock(const char* master_password) {
    lock();
    if (!master_password || !geometry(flash_)) return Status::INVALID_INPUT;
    uint8_t bytes[RECORD_SIZE];
    if (!flash_.read(0, bytes, sizeof(bytes))) return Status::IO_ERROR;
    if (erased(bytes, sizeof(bytes))) return Status::NOT_INITIALIZED;
    if (memcmp(bytes, HEADER_MAGIC, sizeof(HEADER_MAGIC)) != 0 ||
        load32(bytes + COMMIT) != 0 ||
        load32(bytes + 24) != KDF_ITERATIONS) return Status::CORRUPT;
    uint8_t check[sizeof(CHECK)] = {};
    Status status = Status::CRYPTO_ERROR;
    if (derive(master_password, bytes + 8, KDF_ITERATIONS, key_)) {
        status = Status::AUTH_FAILED;
        if (open(key_, bytes + 28, bytes, 28, bytes + 40,
                 sizeof(check), bytes + 56, check) &&
            memcmp(check, CHECK, sizeof(CHECK)) == 0) {
            Scan result;
            status = scan(flash_, key_, 0, 0, result, nullptr);
        }
    }
    wipe(check, sizeof(check));
    wipe(bytes, sizeof(bytes));
    if (status == Status::OK) unlocked_ = true;
    else lock();
    return status;
}

Status Vault::add(const Credential& account, uint32_t& id) {
    id = 0;
    if (!unlocked_) return Status::LOCKED;
    if (!valid(account)) return Status::INVALID_INPUT;
    Scan result;
    Status status = scan(flash_, key_, 0, 0, result, nullptr);
    if (status != Status::OK) return status;
    if (result.maximum_id == UINT32_MAX) return Status::FULL;
    // Normalize unused bytes before encrypting the payload.
    Credential clean{};
    memcpy(clean.name, account.name, strlen(account.name) + 1);
    memcpy(clean.username, account.username, strlen(account.username) + 1);
    memcpy(clean.password, account.password, strlen(account.password) + 1);
    memcpy(clean.totp_secret, account.totp_secret, account.secret_length);
    clean.secret_length = account.secret_length;
    clean.digits = account.secret_length ? account.digits : 6;
    clean.period = account.secret_length ? account.period : 30;
    clean.algorithm = account.secret_length ? account.algorithm : Hash::SHA1;
    const uint32_t new_id = result.maximum_id + 1;
    status = append(flash_, key_, result.next_slot, new_id, ACCOUNT, clean);
    wipe(&clean, sizeof(clean));
    if (status == Status::OK) id = new_id;
    return status;
}

Status Vault::get(uint32_t id, Credential& account) {
    wipe(&account, sizeof(account));
    if (!unlocked_) return Status::LOCKED;
    if (id == 0) return Status::INVALID_INPUT;
    Scan result;
    Status status = scan(flash_, key_, id, 0, result, &account);
    if (status != Status::OK) return status;
    if (!result.found || result.deleted) return Status::NOT_FOUND;
    return Status::OK;
}

Status Vault::remove(uint32_t id) {
    if (!unlocked_) return Status::LOCKED;
    if (id == 0) return Status::INVALID_INPUT;
    Scan result;
    Status status = scan(flash_, key_, id, 0, result, nullptr);
    if (status != Status::OK) return status;
    if (!result.found || result.deleted) return Status::NOT_FOUND;
    Credential empty{};
    return append(flash_, key_, result.next_slot, id, DELETED, empty);
}

Status Vault::next(uint32_t after_id, uint32_t& id) {
    id = 0;
    if (!unlocked_) return Status::LOCKED;
    for (;;) {
        Scan result;
        Status status = scan(flash_, key_, 0, after_id, result, nullptr);
        if (status != Status::OK) return status;
        if (result.candidate == 0) return Status::NOT_FOUND;
        const uint32_t candidate = result.candidate;
        status = scan(flash_, key_, candidate, 0, result, nullptr);
        if (status != Status::OK) return status;
        if (result.found && !result.deleted) {
            id = candidate;
            return Status::OK;
        }
        after_id = candidate;
    }
}
} // namespace vault
