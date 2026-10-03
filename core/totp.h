#pragma once

#include <stddef.h>
#include <stdint.h>
#include "crypto.h"

namespace vault {
    // Decode a Base32 secret into bytes.
    // capacity is the size of out; written receives the decoded length.
    bool decode_base32(const char* text, uint8_t* out, size_t capacity, size_t& written);

    // Generate a TOTP code from a decoded secret and UTC Unix time.
    // Returns false for invalid settings or a crypto failure.
    bool generate_totp(const uint8_t* secret, size_t secret_length, uint64_t unix_time, uint32_t& code, uint8_t digits = 6, uint32_t period = 30, Hash algorithm = Hash::SHA1);
}  // namespace vault