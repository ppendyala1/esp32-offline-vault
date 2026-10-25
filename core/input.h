#pragma once
#include <stdint.h>

namespace vault {
enum class Press { None, Short, Long };
// One event per press. A long press never also produces a short press.
class Button {
public:
    Press update(bool down, uint64_t now) {
        if (down != raw_) { raw_ = down; changed_ = now; }
        if (raw_ != stable_ && now - changed_ >= 30) {
            stable_ = raw_;
            if (stable_) { pressed_ = now; sent_ = false; }
            else if (!sent_) { return Press::Short; }
        }
        if (stable_ && !sent_ && now - pressed_ >= 700) {
            sent_ = true;
            return Press::Long;
        }
        return Press::None;
    }
private:
    bool raw_ = false, stable_ = false, sent_ = false;
    uint64_t changed_ = 0, pressed_ = 0;
};
constexpr uint64_t IDLE_MS = 60000;
constexpr uint64_t REVEAL_MS = 10000;
inline bool expired(uint64_t now, uint64_t since, uint64_t duration) {
    return now - since >= duration;
}
} // namespace vault
