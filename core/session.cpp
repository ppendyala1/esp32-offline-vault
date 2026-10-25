#include "session.h"
#include <string.h>

namespace vault {
void Session::lock() {
    ++lock_epoch;
    manager.lock();
    wipe(entry, sizeof(entry));
    length = 0;
    character = 'a';
    selected = 0;
    revealing = false;
    revealed_at = 0;
    was_unlocked = false;
    dirty = true;
}

void Session::refresh() {
    revealing = false;
    selected = 0;
    if (manager.unlocked()) {
        status = manager.next(0, selected);
        if (status != Status::OK && status != Status::NOT_FOUND) lock();
    }
    dirty = true;
}

void Session::tick(uint64_t now) {
    if (expired(now, last_activity, IDLE_MS) && (manager.unlocked() || length)) lock();
    if (revealing && expired(now, revealed_at, REVEAL_MS)) {
        revealing = false;
        dirty = true;
    }
    if (was_unlocked != manager.unlocked()) {
        if (manager.unlocked()) {
            wipe(entry, sizeof(entry)); length = 0; refresh();
        } else lock();
        was_unlocked = manager.unlocked();
        dirty = true;
    }
}

Status Session::authenticate(const char* password, bool create, uint64_t now) {
    if (now < retry_at) return Status::AUTH_FAILED;
    lock();
    status = create ? manager.initialize(password) : manager.unlock(password);
    if (status != Status::OK) retry_at = now + 2000;
    activity(now);
    dirty = true;
    return status;
}

void Session::move(bool forward) {
    uint32_t id = 0;
    Status result = Status::OK;
    if (forward) {
        result = manager.next(selected, id);
        if (result == Status::NOT_FOUND) result = manager.next(0, id);
    } else {
        result = manager.previous(selected, id);
        if (result == Status::NOT_FOUND) result = manager.previous(0, id);
    }
    if (result == Status::OK || result == Status::NOT_FOUND) selected = id;
    else { status = result; lock(); }
    revealing = false;
    dirty = true;
}

void Session::button(unsigned index, Press press, uint64_t now) {
    if (press == Press::None) return;
    tick(now); // Enforce deadlines before accepting an event.
    activity(now);
    dirty = true;
    if (index == 3 && (manager.unlocked() || press == Press::Long)) { lock(); return; }
    if (manager.unlocked()) {
        if (index < 2) move(index == 1);
        if (index == 2 && selected && !revealing) {
            revealing = true; revealed_at = now;
        } else if (index == 2) revealing = false;
        return;
    }
    if (now < retry_at) return;
    if (index == 0) character = character == 126 ? 32 : character + 1;
    if (index == 1) character = character == 32 ? 126 : character - 1;
    if (index == 3 && length) entry[--length] = 0;
    if (index == 2 && press == Press::Short && length < sizeof(entry) - 1) {
        entry[length++] = character; entry[length] = 0;
    }
    if (index == 2 && press == Press::Long) {
        // authenticate() clears the editor; use a separately wiped copy.
        char password[sizeof(entry)] = {};
        memcpy(password, entry, sizeof(password));
        authenticate(password, false, now);
        wipe(password, sizeof(password));
    }
}
}
