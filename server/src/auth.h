#ifndef AUTH_H
#define AUTH_H

#include <stdbool.h>
#include <stddef.h>

#define MAX_USERS 32
#define MAX_SESSIONS 32
#define USERNAME_MAX 64
#define PASSWORD_ENC_MAX 256
#define TOKEN_LEN 36

typedef struct {
    char username[USERNAME_MAX];
    char enc_password[PASSWORD_ENC_MAX]; /* Encrypted with g_storage_key and base64 encoded */
    char iv[64];                         /* IV used for local storage encryption */
} user_record_t;

typedef struct {
    char token[TOKEN_LEN + 1];
    char username[USERNAME_MAX];
    long login_time;
    bool active;
} user_session_t;

/**
 * Initializes the auth subsystem, loads stored encrypted credentials from disk
 * (or creates default "admin" / "admin123" if no DB exists).
 */
bool auth_init(const char *db_path);

/**
 * Clean up auth subsystem and save DB if needed.
 */
void auth_cleanup(void);

/**
 * Registers a new user from decrypted credentials.
 * Encrypts credentials with g_storage_key and stores them in memory & disk.
 */
bool auth_register_user(const char *username, const char *password, char *err_msg, size_t err_len);

/**
 * Validates login credentials and returns a new session token.
 */
bool auth_login_user(const char *username, const char *password, char *out_token, size_t token_len, char *err_msg, size_t err_len);

/**
 * Validates whether a token is active.
 */
bool auth_validate_token(const char *token, char *out_username, size_t user_len);

/**
 * Logs out and invalidates a session token.
 */
bool auth_logout(const char *token);

#endif /* AUTH_H */
