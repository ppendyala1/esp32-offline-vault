#pragma once
#include <stddef.h>
#include <stdint.h>
namespace vault {
enum class Hash : uint8_t { SHA1=1, SHA256=2, SHA512=3 };
void wipe(void* p, size_t n);
bool random_bytes(uint8_t* out, size_t n);
bool hmac(Hash alg, const uint8_t* key, size_t kn, const uint8_t* msg, size_t mn, uint8_t* out);
bool derive(const char* password, const uint8_t salt[16], uint32_t iterations, uint8_t key[32]);
bool seal(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* aad, size_t an,
          const uint8_t* plain, size_t n, uint8_t* cipher, uint8_t tag[16]);
bool open(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* aad, size_t an,
          const uint8_t* cipher, size_t n, const uint8_t tag[16], uint8_t* plain);
}
