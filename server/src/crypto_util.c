#include "crypto_util.h"
#include "../mongoose/mongoose.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Fixed 32-byte transport key shared with web client */
const uint8_t g_transport_key[AES_KEY_SIZE] = "robot-vacuum-secure-transit-key!";

/* Fixed 32-byte local storage encryption key for credentials on RPi */
const uint8_t g_storage_key[AES_KEY_SIZE] = "robot-local-storage-master-key!";

bool crypto_aes_decrypt(const uint8_t *ciphertext, size_t ciphertext_len,
                        const uint8_t *key, const uint8_t *iv,
                        uint8_t *plaintext_out, size_t *plaintext_len) {
    if (!ciphertext || !key || !iv || !plaintext_out || !plaintext_len) return false;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;

    int len = 0;
    int total_len = 0;
    bool success = false;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv) == 1) {
        if (EVP_DecryptUpdate(ctx, plaintext_out, &len, ciphertext, (int)ciphertext_len) == 1) {
            total_len = len;
            if (EVP_DecryptFinal_ex(ctx, plaintext_out + len, &len) == 1) {
                total_len += len;
                *plaintext_len = (size_t)total_len;
                success = true;
            }
        }
    }

    EVP_CIPHER_CTX_free(ctx);
    return success;
}

bool crypto_aes_encrypt(const uint8_t *plaintext, size_t plaintext_len,
                        const uint8_t *key, const uint8_t *iv,
                        uint8_t *ciphertext_out, size_t *ciphertext_len) {
    if (!plaintext || !key || !iv || !ciphertext_out || !ciphertext_len) return false;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;

    int len = 0;
    int total_len = 0;
    bool success = false;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv) == 1) {
        if (EVP_EncryptUpdate(ctx, ciphertext_out, &len, plaintext, (int)plaintext_len) == 1) {
            total_len = len;
            if (EVP_EncryptFinal_ex(ctx, ciphertext_out + len, &len) == 1) {
                total_len += len;
                *ciphertext_len = (size_t)total_len;
                success = true;
            }
        }
    }

    EVP_CIPHER_CTX_free(ctx);
    return success;
}

char *crypto_decrypt_payload(const char *base64_ciphertext, const char *base64_iv, const uint8_t *key) {
    if (!base64_ciphertext || !base64_iv || !key) return NULL;

    size_t cipher_b64_len = strlen(base64_ciphertext);
    size_t iv_b64_len = strlen(base64_iv);

    size_t raw_cipher_cap = cipher_b64_len + 16;
    uint8_t *raw_cipher = malloc(raw_cipher_cap);
    if (!raw_cipher) return NULL;

    uint8_t raw_iv[AES_IV_SIZE + 8];
    memset(raw_iv, 0, sizeof(raw_iv));

    size_t decoded_iv_len = mg_base64_decode(base64_iv, iv_b64_len, (char *)raw_iv, sizeof(raw_iv));
    if (decoded_iv_len < AES_IV_SIZE) {
        free(raw_cipher);
        return NULL;
    }

    size_t decoded_cipher_len = mg_base64_decode(base64_ciphertext, cipher_b64_len, (char *)raw_cipher, raw_cipher_cap);
    if (decoded_cipher_len == 0 || (decoded_cipher_len % AES_BLOCK_SIZE != 0)) {
        free(raw_cipher);
        return NULL;
    }

    uint8_t *plaintext = malloc(decoded_cipher_len + 16);
    if (!plaintext) {
        free(raw_cipher);
        return NULL;
    }

    size_t plaintext_len = decoded_cipher_len + 16;
    if (!crypto_aes_decrypt(raw_cipher, decoded_cipher_len, key, raw_iv, plaintext, &plaintext_len)) {
        free(raw_cipher);
        free(plaintext);
        return NULL;
    }

    free(raw_cipher);
    plaintext[plaintext_len] = '\0';
    return (char *)plaintext;
}

void crypto_random_bytes(uint8_t *buf, size_t len) {
    if (RAND_bytes(buf, (int)len) != 1) {
        /* Fallback if RAND_bytes fails */
        for (size_t i = 0; i < len; i++) {
            buf[i] = (uint8_t)(rand() & 0xFF);
        }
    }
}
