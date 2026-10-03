#include "totp.h"
#include <stdint.h>
#include <stddef.h>

namespace vault {

bool decode_base32(const char* text, uint8_t* out, size_t capacity, size_t& written) {
    written = 0;

    if (!text || !out) {
        return false;
    }

    uint32_t buffer = 0;
    unsigned bits = 0;
    size_t symbols = 0;
    size_t padding = 0;
    bool valid = true;

    for (const char* p = text; *p; ++p) {
        char c = *p;

        // Allow spaces and lowercase secrets.
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            continue;
        }

        if (c == '=') {
            ++padding;
            continue;
        }

        if (padding != 0) {
            valid = false;
            break;
        }

        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }

        uint8_t value;

        if (c >= 'A' && c <= 'Z') {
            value = static_cast<uint8_t>(c - 'A');
        } 
        else if (c >= '2' && c <= '7') {
            value = static_cast<uint8_t>(c - '2' + 26);
        } 
        else {
            valid = false;
            break;
        }

        ++symbols;
        buffer = (buffer << 5) | value;
        bits += 5;

        if (bits >= 8) {
            bits -= 8;
            if (written >= capacity) {
                valid = false;
                break;
            }

            out[written++] = static_cast<uint8_t>((buffer >> bits) & 0xFF);
        }

        // Retain only the unused bits.
        buffer &= (1U << bits) - 1U;
    }

    const size_t remainder = symbols % 8;
    const bool valid_length = remainder == 0 || remainder == 2 || remainder == 4 || remainder == 5 || remainder == 7;

    if (symbols == 0 || !valid_length || buffer != 0 || (padding != 0 && (remainder == 0 || padding != 8 - remainder))) {
        valid = false;
    }

    if (!valid) {
        wipe(out, written);
        written = 0;
    }

    return valid;
}

bool generate_totp(const uint8_t* secret, size_t secret_length, uint64_t unix_time, uint32_t& code, uint8_t digits, uint32_t period, Hash algorithm) {
    code = 0;

    if (!secret || secret_length == 0 || digits < 6 || digits > 8 || period == 0) {
        return false;
    }

    size_t digest_length;

    switch (algorithm) {
        case Hash::SHA1:
            digest_length = 20;
            break;
        case Hash::SHA256:
            digest_length = 32;
            break;
        case Hash::SHA512:
            digest_length = 64;
            break;
        default:
            return false;
    }

    uint64_t counter = unix_time / period;
    uint8_t message[8];

    // HMAC requires the counter in big-endian byte order.
    for (unsigned i = 0; i < 8; ++i) {
        message[7 - i] = static_cast<uint8_t>(counter & 0xFF);
        counter >>= 8;
    }

    uint8_t digest[64] = {};

    if (!hmac(algorithm, secret, secret_length, message, sizeof(message), digest)) {
        wipe(digest, sizeof(digest));
        return false;
    }

    const unsigned offset = digest[digest_length - 1] & 0x0F;
    const uint32_t value = (static_cast<uint32_t>(digest[offset] & 0x7F) << 24) | (static_cast<uint32_t>(digest[offset + 1]) << 16) | (static_cast<uint32_t>(digest[offset + 2]) << 8) | static_cast<uint32_t>(digest[offset + 3]);

    uint32_t modulus = 1;

    for (uint8_t i = 0; i < digits; ++i) {
        modulus *= 10;
    }

    code = value % modulus;
    wipe(digest, sizeof(digest));

    return true;
}

}  // namespace vault