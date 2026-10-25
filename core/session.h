#pragma once
#include "vault.h"
#include "input.h"

namespace vault {
// All access is serialized by the firmware loop. No credentials are cached here.
class Session {
public:
    explicit Session(Vault& manager) : manager(manager) {}
    ~Session() { lock(); }
    void activity(uint64_t now) { last_activity = now; }
    void tick(uint64_t now);
    void lock();
    Status authenticate(const char* password, bool create, uint64_t now);
    void button(unsigned index, Press press, uint64_t now);
    void refresh();
    Vault& manager;
    char entry[128] = {};
    size_t length = 0;
    char character = 'a';
    uint32_t selected = 0;
    uint32_t lock_epoch = 0;
    uint64_t last_activity = 0, revealed_at = 0, retry_at = 0;
    bool revealing = false, dirty = true;
    Status status = Status::OK;
private:
    bool was_unlocked = false;
    void move(bool forward);
};
}
