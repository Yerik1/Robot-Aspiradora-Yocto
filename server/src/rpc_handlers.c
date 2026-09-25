#include "rpc_handlers.h"
#include "auth.h"
#include "crypto_util.h"
#include "sim_robot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void rpc_auth_login(struct mg_rpc_req *r) {
    char *enc_b64 = mg_json_get_str(r->frame, "$.params.encrypted");
    char *iv_b64 = mg_json_get_str(r->frame, "$.params.iv");

    if (!enc_b64 || !iv_b64) {
        free(enc_b64);
        free(iv_b64);
        mg_rpc_err(r, 400, "Missing encrypted credentials or IV");
        return;
    }

    char *plaintext = crypto_decrypt_payload(enc_b64, iv_b64, g_transport_key);
    free(enc_b64);
    free(iv_b64);

    if (!plaintext) {
        printf("\033[31m[AUTH]\033[0m Failed to decrypt login payload (invalid key or bad ciphertext)\n");
        mg_rpc_err(r, 401, "Decryption failed on server");
        return;
    }

    struct mg_str pt_str = mg_str(plaintext);
    char *user = mg_json_get_str(pt_str, "$.username");
    char *pass = mg_json_get_str(pt_str, "$.password");
    free(plaintext);

    if (!user || !pass) {
        free(user);
        free(pass);
        mg_rpc_err(r, 400, "Invalid JSON inside encrypted payload");
        return;
    }

    char token[TOKEN_LEN + 1];
    char err[128];
    if (auth_login_user(user, pass, token, sizeof(token), err, sizeof(err))) {
        mg_rpc_ok(r, "{%m:%m,%m:%m}",
                  MG_ESC("token"), MG_ESC(token),
                  MG_ESC("username"), MG_ESC(user));
    } else {
        mg_rpc_err(r, 401, "%s", err);
    }

    free(user);
    free(pass);
}

static void rpc_auth_register(struct mg_rpc_req *r) {
    char *enc_b64 = mg_json_get_str(r->frame, "$.params.encrypted");
    char *iv_b64 = mg_json_get_str(r->frame, "$.params.iv");

    if (!enc_b64 || !iv_b64) {
        free(enc_b64);
        free(iv_b64);
        mg_rpc_err(r, 400, "Missing encrypted credentials or IV");
        return;
    }

    char *plaintext = crypto_decrypt_payload(enc_b64, iv_b64, g_transport_key);
    free(enc_b64);
    free(iv_b64);

    if (!plaintext) {
        printf("\033[31m[AUTH]\033[0m Failed to decrypt registration payload\n");
        mg_rpc_err(r, 401, "Decryption failed on server");
        return;
    }

    struct mg_str pt_str = mg_str(plaintext);
    char *user = mg_json_get_str(pt_str, "$.username");
    char *pass = mg_json_get_str(pt_str, "$.password");
    free(plaintext);

    if (!user || !pass) {
        free(user);
        free(pass);
        mg_rpc_err(r, 400, "Invalid JSON inside encrypted payload");
        return;
    }

    char err[128];
    if (auth_register_user(user, pass, err, sizeof(err))) {
        mg_rpc_ok(r, "{%m:true,%m:%m}",
                  MG_ESC("success"),
                  MG_ESC("message"), MG_ESC("User registered successfully"));
    } else {
        mg_rpc_err(r, 400, "%s", err);
    }

    free(user);
    free(pass);
}

static void rpc_auth_logout(struct mg_rpc_req *r) {
    char *token = mg_json_get_str(r->frame, "$.params.token");
    if (token) {
        auth_logout(token);
        free(token);
    }
    mg_rpc_ok(r, "{%m:true}", MG_ESC("success"));
}

static void rpc_robot_set_mode(struct mg_rpc_req *r) {
    char *mode = mg_json_get_str(r->frame, "$.params.mode");
    if (!mode) {
        mg_rpc_err(r, 400, "Missing mode parameter ('autonomous' or 'manual')");
        return;
    }

    if (strcmp(mode, "autonomous") == 0) {
        sim_robot_set_mode(MODE_AUTONOMOUS);
    } else if (strcmp(mode, "manual") == 0) {
        sim_robot_set_mode(MODE_MANUAL);
    } else {
        free(mode);
        mg_rpc_err(r, 400, "Unknown mode: must be 'autonomous' or 'manual'");
        return;
    }

    mg_rpc_ok(r, "{%m:%m}", MG_ESC("mode"), MG_ESC(mode));
    free(mode);
}

static void rpc_robot_move(struct mg_rpc_req *r) {
    char *dir = mg_json_get_str(r->frame, "$.params.direction");
    double speed = 50.0;
    double radius = 50.0;

    mg_json_get_num(r->frame, "$.params.speed", &speed);
    mg_json_get_num(r->frame, "$.params.radius", &radius);

    if (speed < 0) speed = 0;
    if (speed > 100) speed = 100;
    if (radius < 0) radius = 0;
    if (radius > 100) radius = 100;

    if (!dir) {
        mg_rpc_err(r, 400, "Missing direction parameter");
        return;
    }

    bool ok = sim_robot_move(dir, (uint8_t)speed, (uint8_t)radius);
    free(dir);

    if (ok) {
        mg_rpc_ok(r, "{%m:true}", MG_ESC("success"));
    } else {
        mg_rpc_err(r, 403, "Cannot execute manual move while in autonomous mode");
    }
}

static void rpc_robot_stop(struct mg_rpc_req *r) {
    sim_robot_stop();
    mg_rpc_ok(r, "{%m:true}", MG_ESC("success"));
}

static void rpc_robot_vacuum(struct mg_rpc_req *r) {
    bool enabled = false;
    if (!mg_json_get_bool(r->frame, "$.params.enabled", &enabled)) {
        mg_rpc_err(r, 400, "Missing boolean enabled parameter");
        return;
    }

    sim_robot_set_vacuum(enabled);
    mg_rpc_ok(r, "{%m:%s}", MG_ESC("vacuum"), enabled ? "true" : "false");
}

static void rpc_audio_play(struct mg_rpc_req *r) {
    char *file = mg_json_get_str(r->frame, "$.params.file");
    sim_robot_audio_play(file ? file : "ambient_chill.mp3");
    if (file) free(file);
    mg_rpc_ok(r, "{%m:true}", MG_ESC("success"));
}

static void rpc_audio_pause(struct mg_rpc_req *r) {
    sim_robot_audio_pause();
    mg_rpc_ok(r, "{%m:true}", MG_ESC("success"));
}

static void rpc_audio_resume(struct mg_rpc_req *r) {
    sim_robot_audio_resume();
    mg_rpc_ok(r, "{%m:true}", MG_ESC("success"));
}

static void rpc_audio_stop(struct mg_rpc_req *r) {
    sim_robot_audio_stop();
    mg_rpc_ok(r, "{%m:true}", MG_ESC("success"));
}

static void rpc_audio_volume(struct mg_rpc_req *r) {
    double vol = 70.0;
    mg_json_get_num(r->frame, "$.params.volume", &vol);
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    sim_robot_audio_set_volume((uint8_t)vol);
    mg_rpc_ok(r, "{%m:%d}", MG_ESC("volume"), (int)vol);
}

static void rpc_audio_notification(struct mg_rpc_req *r) {
    double evt = 0.0;
    mg_json_get_num(r->frame, "$.params.event", &evt);
    sim_robot_audio_play_notification((notif_event_t)(int)evt);
    mg_rpc_ok(r, "{%m:true}", MG_ESC("success"));
}

static void rpc_audio_list_files(struct mg_rpc_req *r) {
    mg_rpc_ok(r, "[%m,%m,%m,%m]",
              MG_ESC("ambient_chill.mp3"),
              MG_ESC("cyber_beat.mp3"),
              MG_ESC("retro_wave.mp3"),
              MG_ESC("smooth_jazz.mp3"));
}

static void rpc_map_get(struct mg_rpc_req *r) {
    robot_state_t *st = sim_robot_get_state();

    /* Format 2D grid as a compact flat array */
    struct mg_iobuf io = {0, 0, 0, 256};
    mg_iobuf_init(&io, 4096, 256);

    mg_iobuf_add(&io, io.len, "[", 1);
    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            char num[8];
            int n = snprintf(num, sizeof(num), "%d%s", st->grid[y][x],
                             (y == MAP_HEIGHT - 1 && x == MAP_WIDTH - 1) ? "" : ",");
            mg_iobuf_add(&io, io.len, num, n);
        }
    }
    mg_iobuf_add(&io, io.len, "]", 1);

    mg_rpc_ok(r, "{%m:%d,%m:%d,%m:{%m:%g,%m:%g,%m:%g},%m:%.*s}",
              MG_ESC("width"), MAP_WIDTH,
              MG_ESC("height"), MAP_HEIGHT,
              MG_ESC("robot"),
              MG_ESC("x"), st->robot_x,
              MG_ESC("y"), st->robot_y,
              MG_ESC("heading"), st->heading_deg,
              MG_ESC("grid"), (int)io.len, (char *)io.buf);

    mg_iobuf_free(&io);
}

static void rpc_map_reset(struct mg_rpc_req *r) {
    sim_robot_map_reset();
    mg_rpc_ok(r, "{%m:true}", MG_ESC("success"));
}

static void rpc_system_status(struct mg_rpc_req *r) {
    robot_state_t *st = sim_robot_get_state();
    mg_rpc_ok(r, "{%m:%m,%m:%m,%m:%d}",
              MG_ESC("firmware"), MG_ESC("Robot-Aspiradora-Yocto v0.1.0"),
              MG_ESC("mode"), MG_ESC(st->mode == MODE_AUTONOMOUS ? "autonomous" : "manual"),
              MG_ESC("vacuum"), st->vacuum_on ? 1 : 0);
}

void rpc_handlers_init(struct mg_rpc **head) {
    mg_rpc_add(head, mg_str("auth.login"), rpc_auth_login, NULL);
    mg_rpc_add(head, mg_str("auth.register"), rpc_auth_register, NULL);
    mg_rpc_add(head, mg_str("auth.logout"), rpc_auth_logout, NULL);

    mg_rpc_add(head, mg_str("robot.set_mode"), rpc_robot_set_mode, NULL);
    mg_rpc_add(head, mg_str("robot.move"), rpc_robot_move, NULL);
    mg_rpc_add(head, mg_str("robot.stop"), rpc_robot_stop, NULL);
    mg_rpc_add(head, mg_str("robot.vacuum"), rpc_robot_vacuum, NULL);

    mg_rpc_add(head, mg_str("audio.play"), rpc_audio_play, NULL);
    mg_rpc_add(head, mg_str("audio.pause"), rpc_audio_pause, NULL);
    mg_rpc_add(head, mg_str("audio.resume"), rpc_audio_resume, NULL);
    mg_rpc_add(head, mg_str("audio.stop"), rpc_audio_stop, NULL);
    mg_rpc_add(head, mg_str("audio.volume"), rpc_audio_volume, NULL);
    mg_rpc_add(head, mg_str("audio.notification"), rpc_audio_notification, NULL);
    mg_rpc_add(head, mg_str("audio.list_files"), rpc_audio_list_files, NULL);

    mg_rpc_add(head, mg_str("map.get"), rpc_map_get, NULL);
    mg_rpc_add(head, mg_str("map.reset"), rpc_map_reset, NULL);
    mg_rpc_add(head, mg_str("system.status"), rpc_system_status, NULL);
    mg_rpc_add(head, mg_str("rpc.list"), mg_rpc_list, head);
}
