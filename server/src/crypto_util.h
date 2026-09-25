#ifndef CRYPTO_UTIL_H
#define CRYPTO_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define AES_KEY_SIZE 32
#define AES_IV_SIZE  16
#define AES_BLOCK_SIZE 16

/* Default transport key (32 bytes for AES-256) shared with web client */
extern const uint8_t g_transport_key[AES_KEY_SIZE];

/* Local storage master key for Raspberry Pi memory/disk encryption */
extern const uint8_t g_storage_key[AES_KEY_SIZE];

/**
 * Decrypts AES-256-CBC ciphertext with PKCS#7 padding.
 * @param ciphertext      Raw encrypted bytes.
 * @param ciphertext_len  Length of ciphertext (must be multiple of 16).
 * @param key             32-byte AES key.
 * @param iv              16-byte IV.
 * @param plaintext_out   Output buffer allocated by caller.
 * @param plaintext_len   In: capacity of buffer; Out: actual decrypted length.
 * @return true on success, false on failure (bad padding/key).
 */
bool crypto_aes_decrypt(const uint8_t *ciphertext, size_t ciphertext_len,
                        const uint8_t *key, const uint8_t *iv,
                        uint8_t *plaintext_out, size_t *plaintext_len);

/**
 * Encrypts plaintext with AES-256-CBC and PKCS#7 padding.
 * @param plaintext       Raw plaintext bytes.
 * @param plaintext_len   Length of plaintext.
 * @param key             32-byte AES key.
 * @param iv              16-byte IV.
 * @param ciphertext_out  Output buffer allocated by caller.
 * @param ciphertext_len  In: capacity of buffer; Out: actual encrypted length.
 * @return true on success, false on failure.
 */
bool crypto_aes_encrypt(const uint8_t *plaintext, size_t plaintext_len,
                        const uint8_t *key, const uint8_t *iv,
                        uint8_t *ciphertext_out, size_t *ciphertext_len);

/**
 * Decrypts a base64-encoded encrypted string with base64-encoded IV.
 * Allocates and returns a null-terminated decrypted string (caller frees),
 * or NULL on failure.
 */
char *crypto_decrypt_payload(const char *base64_ciphertext, const char *base64_iv, const uint8_t *key);

/**
 * Generates random bytes (for IVs, salts, etc.).
 */
void crypto_random_bytes(uint8_t *buf, size_t len);

#endif /* CRYPTO_UTIL_H */
