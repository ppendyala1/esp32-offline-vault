#include "../core/crypto.h"
#include <mbedtls/md.h>
#include <string.h>
#include <mbedtls/pkcs5.h>
namespace vault {

void wipe(void* p, size_t n) {
    volatile uint8_t* bytes = static_cast<volatile uint8_t*>(p);

    while (n > 0) {
        *bytes++ = 0;
        --n;
    }
}

bool hmac(Hash algorithm, const uint8_t* key, size_t key_length, const uint8_t* message, size_t message_length, uint8_t* out) {
    if (!key || key_length == 0 || !out ||
        (!message && message_length != 0)) {
        return false;
    }

    mbedtls_md_type_t type;

    switch (algorithm) {
        case Hash::SHA1:
            type = MBEDTLS_MD_SHA1;
            break;
        case Hash::SHA256:
            type = MBEDTLS_MD_SHA256;
            break;
        case Hash::SHA512:
            type = MBEDTLS_MD_SHA512;
            break;
        default:
            return false;
    }

    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(type);

    if (!info) {
        return false;
    }

    // Supply a valid pointer for an empty message.
    const uint8_t empty = 0;

    if (!message) {
        message = &empty;
    }

    const int result = mbedtls_md_hmac(
        info,
        key, key_length,
        message, message_length,
        out
    );

    if (result != 0) {
        wipe(out, mbedtls_md_get_size(info));
        return false;
    }

    return true;
}
bool derive(const char* password, const uint8_t salt[16], uint32_t iterations, uint8_t key[32]) {
    if (!key) {
        return false;
    }

    wipe(key, 32);

    if (!password || !salt || iterations == 0) {
        return false;
    }

    const mbedtls_md_info_t* info =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    if (!info) {
        return false;
    }

    mbedtls_md_context_t context;
    mbedtls_md_init(&context);

    int result = mbedtls_md_setup(&context, info, 1);

    if (result == 0) {
        result = mbedtls_pkcs5_pbkdf2_hmac(
            &context,
            reinterpret_cast<const unsigned char*>(password),
            strlen(password),
            salt, 16,
            iterations,
            32, key
        );
    }

    mbedtls_md_free(&context);

    if (result != 0) {
        wipe(key, 32);
        return false;
    }

    return true;
}

}  // namespace vault