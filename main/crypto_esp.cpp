#include "../core/crypto.h"

#include "psa/crypto.h"
#include "esp_random.h"
#include "bootloader_random.h"
#include <string.h>
#include <stdlib.h>
namespace vault {

void wipe(void* p, size_t n) {
    volatile uint8_t* bytes =
        static_cast<volatile uint8_t*>(p);

    while (n > 0) {
        *bytes++ = 0;
        --n;
    }
}

bool random_bytes(uint8_t* out, size_t n) {
    if (n == 0) {
        return true;
    }

    if (!out) {
        return false;
    }

    // Call only while ADC, Wi-Fi, and Bluetooth are inactive.
    bootloader_random_enable();
    esp_fill_random(out, n);
    bootloader_random_disable();

    return true;
}

bool hmac(Hash algorithm,
          const uint8_t* key, size_t key_length,
          const uint8_t* message, size_t message_length,
          uint8_t* out) {
    if (!key || key_length == 0 || !out ||
        (!message && message_length != 0)) {
        return false;
    }

    psa_algorithm_t hash;
    size_t digest_length;

    switch (algorithm) {
        case Hash::SHA1:
            hash = PSA_ALG_SHA_1;
            digest_length = 20;
            break;
        case Hash::SHA256:
            hash = PSA_ALG_SHA_256;
            digest_length = 32;
            break;
        case Hash::SHA512:
            hash = PSA_ALG_SHA_512;
            digest_length = 64;
            break;
        default:
            return false;
    }

    wipe(out, digest_length);

    if (psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }

    const psa_algorithm_t mac = PSA_ALG_HMAC(hash);

    psa_key_attributes_t attributes =
        PSA_KEY_ATTRIBUTES_INIT;

    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_usage_flags(
        &attributes, PSA_KEY_USAGE_SIGN_MESSAGE
    );
    psa_set_key_algorithm(&attributes, mac);

    // Imported keys use volatile storage by default.
    psa_key_id_t key_id = 0;

    psa_status_t status = psa_import_key(
        &attributes, key, key_length, &key_id
    );

    psa_reset_key_attributes(&attributes);

    const uint8_t empty = 0;
    size_t written = 0;

    if (status == PSA_SUCCESS) {
        status = psa_mac_compute(
            key_id, mac,
            message ? message : &empty,
            message_length,
            out, digest_length, &written
        );
    }

    if (key_id != 0) {
        const psa_status_t cleanup = psa_destroy_key(key_id);

        if (status == PSA_SUCCESS) {
            status = cleanup;
        }
    }

    if (status != PSA_SUCCESS || written != digest_length) {
        wipe(out, digest_length);
        return false;
    }

    return true;
}

bool derive(const char* password,
            const uint8_t salt[16],
            uint32_t iterations,
            uint8_t key[32]) {
    if (!key) {
        return false;
    }

    wipe(key, 32);

    if (!password || !salt || iterations == 0) {
        return false;
    }

    if (psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }

    psa_key_derivation_operation_t operation =
        PSA_KEY_DERIVATION_OPERATION_INIT;

    psa_status_t status = psa_key_derivation_setup(
        &operation,
        PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256)
    );

    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_input_integer(
            &operation,
            PSA_KEY_DERIVATION_INPUT_COST,
            iterations
        );
    }

    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_input_bytes(
            &operation,
            PSA_KEY_DERIVATION_INPUT_SALT,
            salt, 16
        );
    }

    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_input_bytes(
            &operation,
            PSA_KEY_DERIVATION_INPUT_PASSWORD,
            reinterpret_cast<const uint8_t*>(password),
            strlen(password)
        );
    }

    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_output_bytes(
            &operation, key, 32
        );
    }

    const psa_status_t cleanup =
        psa_key_derivation_abort(&operation);

    if (status != PSA_SUCCESS || cleanup != PSA_SUCCESS) {
        wipe(key, 32);
        return false;
    }

    return true;
}

static bool import_aes(const uint8_t key[32],
                       psa_key_usage_t usage,
                       psa_key_id_t& id) {
    id = 0;

    if (psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }

    psa_key_attributes_t attributes =
        PSA_KEY_ATTRIBUTES_INIT;

    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, usage);
    psa_set_key_algorithm(&attributes, PSA_ALG_GCM);

    const psa_status_t status = psa_import_key(
        &attributes, key, 32, &id
    );

    psa_reset_key_attributes(&attributes);

    return status == PSA_SUCCESS;
}

bool seal(const uint8_t key[32],
          const uint8_t nonce[12],
          const uint8_t* aad, size_t an,
          const uint8_t* plain, size_t n,
          uint8_t* cipher,
          uint8_t tag[16]) {
    if (!key || !nonce || !tag ||
        (!aad && an != 0) ||
        (!plain && n != 0) ||
        (!cipher && n != 0) ||
        n > SIZE_MAX - 16) {
        return false;
    }

    // PSA returns ciphertext followed by the authentication tag.
    uint8_t* combined =
        static_cast<uint8_t*>(malloc(n + 16));

    if (!combined) {
        if (cipher) wipe(cipher, n);
        wipe(tag, 16);
        return false;
    }

    psa_key_id_t id = 0;
    size_t written = 0;
    uint8_t empty = 0;
    bool success = false;

    if (import_aes(key, PSA_KEY_USAGE_ENCRYPT, id)) {
        const psa_status_t status = psa_aead_encrypt(
            id, PSA_ALG_GCM,
            nonce, 12,
            aad ? aad : &empty, an,
            plain ? plain : &empty, n,
            combined, n + 16, &written
        );

        success = status == PSA_SUCCESS &&
                  written == n + 16;
    }

    if (id != 0) {
        const psa_status_t cleanup = psa_destroy_key(id);
        success = success && cleanup == PSA_SUCCESS;
    }

    if (success) {
        if (n != 0) memcpy(cipher, combined, n);
        memcpy(tag, combined + n, 16);
    } else {
        if (cipher) wipe(cipher, n);
        wipe(tag, 16);
    }

    wipe(combined, n + 16);
    free(combined);

    return success;
}

bool open(const uint8_t key[32],
          const uint8_t nonce[12],
          const uint8_t* aad, size_t an,
          const uint8_t* cipher, size_t n,
          const uint8_t tag[16],
          uint8_t* plain) {
    if (!key || !nonce || !tag ||
        (!aad && an != 0) ||
        (!cipher && n != 0) ||
        (!plain && n != 0) ||
        n > SIZE_MAX - 16) {
        return false;
    }

    uint8_t* combined =
        static_cast<uint8_t*>(malloc(n + 16));

    if (!combined) {
        if (plain) wipe(plain, n);
        return false;
    }

    if (n != 0) memcpy(combined, cipher, n);
    memcpy(combined + n, tag, 16);

    psa_key_id_t id = 0;
    size_t written = 0;
    uint8_t empty = 0;
    bool success = false;

    if (import_aes(key, PSA_KEY_USAGE_DECRYPT, id)) {
        const psa_status_t status = psa_aead_decrypt(
            id, PSA_ALG_GCM,
            nonce, 12,
            aad ? aad : &empty, an,
            combined, n + 16,
            plain ? plain : &empty, n,
            &written
        );

        success = status == PSA_SUCCESS && written == n;
    }

    if (id != 0) {
        const psa_status_t cleanup = psa_destroy_key(id);
        success = success && cleanup == PSA_SUCCESS;
    }

    if (!success && plain) {
        wipe(plain, n);
    }

    wipe(combined, n + 16);
    free(combined);

    return success;
}

}  // namespace vault