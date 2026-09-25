#include "auth.h"
#include "crypto_util.h"
#include "../mongoose/mongoose.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static user_record_t s_users[MAX_USERS];
static size_t s_user_count = 0;

static user_session_t s_sessions[MAX_SESSIONS];
static char s_db_path[256] = "users.db";

static bool save_db(void) {
    FILE *f = fopen(s_db_path, "w");
    if (!f) return false;
    for (size_t i = 0; i < s_user_count; i++) {
        fprintf(f, "%s:%s:%s\n", s_users[i].username, s_users[i].enc_password, s_users[i].iv);
    }
    fclose(f);
    return true;
}

static bool load_db(void) {
    FILE *f = fopen(s_db_path, "r");
    if (!f) return false;

    char line[512];
    s_user_count = 0;
    while (fgets(line, sizeof(line), f) && s_user_count < MAX_USERS) {
        // Strip trailing newline
        line[strcspn(line, "\r\n")] = 0;
        char *user = strtok(line, ":");
        char *enc_pass = strtok(NULL, ":");
        char *iv = strtok(NULL, ":");

        if (user && enc_pass && iv) {
            snprintf(s_users[s_user_count].username, sizeof(s_users[s_user_count].username), "%s", user);
            snprintf(s_users[s_user_count].enc_password, sizeof(s_users[s_user_count].enc_password), "%s", enc_pass);
            snprintf(s_users[s_user_count].iv, sizeof(s_users[s_user_count].iv), "%s", iv);
            s_user_count++;
        }
    }
    fclose(f);
    return true;
}

bool auth_init(const char *db_path) {
    if (db_path && strlen(db_path) > 0) {
        snprintf(s_db_path, sizeof(s_db_path), "%s", db_path);
    }
    memset(s_users, 0, sizeof(s_users));
    memset(s_sessions, 0, sizeof(s_sessions));
    s_user_count = 0;

    if (!load_db() || s_user_count == 0) {
        printf("\033[33m[AUTH]\033[0m No user database found. Creating default admin account ('admin' / 'admin123')...\n");
        char err[128];
        auth_register_user("admin", "admin123", err, sizeof(err));
    } else {
        printf("\033[32m[AUTH]\033[0m Loaded %zu registered user(s) from encrypted storage: %s\n", s_user_count, s_db_path);
    }
    return true;
}

void auth_cleanup(void) {
    save_db();
}

bool auth_register_user(const char *username, const char *password, char *err_msg, size_t err_len) {
    if (!username || !password || strlen(username) < 3 || strlen(password) < 4) {
        if (err_msg) snprintf(err_msg, err_len, "Username must be >= 3 chars, password >= 4 chars");
        return false;
    }

    if (s_user_count >= MAX_USERS) {
        if (err_msg) snprintf(err_msg, err_len, "Maximum user limit reached on device");
        return false;
    }

    for (size_t i = 0; i < s_user_count; i++) {
        if (strcmp(s_users[i].username, username) == 0) {
            if (err_msg) snprintf(err_msg, err_len, "Username already exists");
            return false;
        }
    }

    /* Encrypt password with local storage master key */
    uint8_t iv[AES_IV_SIZE];
    crypto_random_bytes(iv, sizeof(iv));

    size_t pass_len = strlen(password);
    size_t enc_buf_len = pass_len + 32;
    uint8_t *enc_buf = malloc(enc_buf_len);
    if (!enc_buf) {
        if (err_msg) snprintf(err_msg, err_len, "Out of memory");
        return false;
    }

    size_t actual_enc_len = enc_buf_len;
    if (!crypto_aes_encrypt((const uint8_t *)password, pass_len, g_storage_key, iv, enc_buf, &actual_enc_len)) {
        free(enc_buf);
        if (err_msg) snprintf(err_msg, err_len, "Local encryption error");
        return false;
    }

    /* Base64 encode encrypted password and IV */
    char b64_enc[PASSWORD_ENC_MAX];
    char b64_iv[64];
    mg_base64_encode(enc_buf, actual_enc_len, b64_enc, sizeof(b64_enc));
    mg_base64_encode(iv, sizeof(iv), b64_iv, sizeof(b64_iv));
    free(enc_buf);

    user_record_t *rec = &s_users[s_user_count];
    snprintf(rec->username, sizeof(rec->username), "%s", username);
    snprintf(rec->enc_password, sizeof(rec->enc_password), "%s", b64_enc);
    snprintf(rec->iv, sizeof(rec->iv), "%s", b64_iv);
    s_user_count++;

    save_db();
    printf("\033[32m[AUTH]\033[0m Registered new user '%s' (encrypted credentials stored locally)\n", username);
    return true;
}

bool auth_login_user(const char *username, const char *password, char *out_token, size_t token_len, char *err_msg, size_t err_len) {
    if (!username || !password) {
        if (err_msg) snprintf(err_msg, err_len, "Invalid credentials");
        return false;
    }

    for (size_t i = 0; i < s_user_count; i++) {
        if (strcmp(s_users[i].username, username) == 0) {
            /* Decode stored IV and ciphertext */
            uint8_t iv[AES_IV_SIZE + 8];
            size_t iv_len = mg_base64_decode(s_users[i].iv, strlen(s_users[i].iv), (char *)iv, sizeof(iv));

            size_t enc_cap = strlen(s_users[i].enc_password) + 16;
            uint8_t *enc_raw = malloc(enc_cap);
            if (!enc_raw) {
                if (err_msg) snprintf(err_msg, err_len, "Memory allocation error");
                return false;
            }

            size_t enc_len = mg_base64_decode(s_users[i].enc_password, strlen(s_users[i].enc_password), (char *)enc_raw, enc_cap);
            uint8_t *dec_pass = malloc(enc_len + 16);
            size_t dec_len = enc_len + 16;

            bool decrypt_ok = (iv_len >= AES_IV_SIZE) &&
                              crypto_aes_decrypt(enc_raw, enc_len, g_storage_key, iv, dec_pass, &dec_len);
            free(enc_raw);

            if (!decrypt_ok) {
                free(dec_pass);
                if (err_msg) snprintf(err_msg, err_len, "Decryption error for stored credentials");
                return false;
            }
            dec_pass[dec_len] = '\0';

            bool match = (strcmp((char *)dec_pass, password) == 0);
            free(dec_pass);

            if (!match) {
                printf("\033[31m[AUTH]\033[0m Failed login attempt for user '%s' (incorrect password)\n", username);
                if (err_msg) snprintf(err_msg, err_len, "Invalid username or password");
                return false;
            }

            /* Generate session token */
            uint8_t rand_token[16];
            crypto_random_bytes(rand_token, sizeof(rand_token));
            char hex_token[TOKEN_LEN + 1];
            for (int k = 0; k < 16; k++) {
                sprintf(&hex_token[k * 2], "%02x", rand_token[k]);
            }
            hex_token[32] = '\0';

            /* Store session */
            for (size_t s = 0; s < MAX_SESSIONS; s++) {
                if (!s_sessions[s].active || s == MAX_SESSIONS - 1) {
                    s_sessions[s].active = true;
                    snprintf(s_sessions[s].token, sizeof(s_sessions[s].token), "%s", hex_token);
                    snprintf(s_sessions[s].username, sizeof(s_sessions[s].username), "%s", username);
                    s_sessions[s].login_time = (long)time(NULL);
                    break;
                }
            }

            if (out_token) {
                snprintf(out_token, token_len, "%s", hex_token);
            }
            printf("\033[32m[AUTH]\033[0m User '%s' logged in successfully. Session token generated.\n", username);
            return true;
        }
    }

    printf("\033[31m[AUTH]\033[0m Failed login attempt: user '%s' not found\n", username);
    if (err_msg) snprintf(err_msg, err_len, "Invalid username or password");
    return false;
}

bool auth_validate_token(const char *token, char *out_username, size_t user_len) {
    if (!token) return false;
    for (size_t s = 0; s < MAX_SESSIONS; s++) {
        if (s_sessions[s].active && strcmp(s_sessions[s].token, token) == 0) {
            if (out_username) {
                snprintf(out_username, user_len, "%s", s_sessions[s].username);
            }
            return true;
        }
    }
    return false;
}

bool auth_logout(const char *token) {
    if (!token) return false;
    for (size_t s = 0; s < MAX_SESSIONS; s++) {
        if (s_sessions[s].active && strcmp(s_sessions[s].token, token) == 0) {
            s_sessions[s].active = false;
            printf("\033[33m[AUTH]\033[0m User '%s' logged out.\n", s_sessions[s].username);
            return true;
        }
    }
    return false;
}
