#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>

#include "../mongoose/mongoose.h"
#include "crypto_util.h"
#include "auth.h"
#include "sim_robot.h"
#include "rpc_handlers.h"

static const char *s_listen_on = "http://0.0.0.0:8080";
static char s_web_root[1024] = "../client";
static struct mg_rpc *s_rpc_head = NULL;
static bool s_running = true;

static void sig_handler(int sig) {
    (void)sig;
    s_running = false;
}

static void broadcast_telemetry(struct mg_mgr *mgr) {
    robot_state_t *st = sim_robot_get_state();

    const char *audio_state_str = "stopped";
    if (st->audio_state == SIM_AUDIO_PLAYING) audio_state_str = "playing";
    else if (st->audio_state == SIM_AUDIO_PAUSED) audio_state_str = "paused";

    /* Format telemetry JSON */
    char buf[1024];
    int n = snprintf(buf, sizeof(buf),
        "{\"method\":\"robot.telemetry\",\"params\":{"
        "\"mode\":\"%s\","
        "\"vacuum\":%s,"
        "\"direction\":\"%s\","
        "\"speed\":%d,"
        "\"radius\":%d,"
        "\"motors\":{\"left\":%d,\"right\":%d},"
        "\"sensors\":{\"front_cm\":%.1f,\"left_cm\":%.1f,\"right_cm\":%.1f,"
                     "\"obs_front\":%s,\"obs_left\":%s,\"obs_right\":%s},"
        "\"leds\":{\"autonomous\":%s,\"manual\":%s,\"obstacle_alert\":%s,\"system_on\":%s},"
        "\"audio\":{\"state\":\"%s\",\"track\":\"%s\",\"volume\":%d},"
        "\"robot\":{\"x\":%.2f,\"y\":%.2f,\"heading\":%.1f}"
        "}}",
        st->mode == MODE_AUTONOMOUS ? "autonomous" : "manual",
        st->vacuum_on ? "true" : "false",
        st->direction,
        st->speed_percent,
        st->radius_percent,
        st->left_motor_speed,
        st->right_motor_speed,
        st->front_distance_cm,
        st->left_distance_cm,
        st->right_distance_cm,
        st->front_obstacle ? "true" : "false",
        st->left_obstacle ? "true" : "false",
        st->right_obstacle ? "true" : "false",
        st->led_autonomous ? "true" : "false",
        st->led_manual ? "true" : "false",
        st->led_obstacle_alert ? "true" : "false",
        st->led_system_on ? "true" : "false",
        audio_state_str,
        st->current_track,
        st->volume_percent,
        st->robot_x,
        st->robot_y,
        st->heading_deg
    );

    /* Broadcast map updates if cells changed */
    char map_buf[1024];
    int map_n = 0;
    if (st->update_count > 0) {
        struct mg_iobuf map_io = {0, 0, 0, 256};
        mg_iobuf_init(&map_io, 1024, 256);
        mg_iobuf_add(&map_io, map_io.len, "{\"method\":\"robot.map_update\",\"params\":{\"robot\":{", 49);
        char rob_str[64];
        int rlen = snprintf(rob_str, sizeof(rob_str), "\"x\":%.2f,\"y\":%.2f,\"heading\":%.1f},\"updates\":[",
                            st->robot_x, st->robot_y, st->heading_deg);
        mg_iobuf_add(&map_io, map_io.len, rob_str, rlen);

        for (size_t i = 0; i < st->update_count; i++) {
            char u[48];
            int ulen = snprintf(u, sizeof(u), "{\"x\":%d,\"y\":%d,\"val\":%d}%s",
                                st->updates[i].x, st->updates[i].y, st->updates[i].val,
                                (i == st->update_count - 1) ? "" : ",");
            mg_iobuf_add(&map_io, map_io.len, u, ulen);
        }
        mg_iobuf_add(&map_io, map_io.len, "]}}", 3);
        if (map_io.len < sizeof(map_buf)) {
            memcpy(map_buf, map_io.buf, map_io.len);
            map_n = (int)map_io.len;
        }
        mg_iobuf_free(&map_io);
    }

    for (struct mg_connection *c = mgr->conns; c != NULL; c = c->next) {
        if (c->data[0] != 'W') continue;
        mg_ws_send(c, buf, n, WEBSOCKET_OP_TEXT);
        if (map_n > 0) {
            mg_ws_send(c, map_buf, map_n, WEBSOCKET_OP_TEXT);
        }
    }
}

static void timer_tick(void *arg) {
    struct mg_mgr *mgr = (struct mg_mgr *)arg;
    sim_robot_tick(0.2); /* 200 ms tick */
    broadcast_telemetry(mgr);
}

static void server_fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_OPEN) {
        // connection opened
    } else if (ev == MG_EV_WS_OPEN) {
        c->data[0] = 'W'; /* Mark as established WS connection */
        char rem_ip[64];
        mg_snprintf(rem_ip, sizeof(rem_ip), "%M", mg_print_ip_port, &c->rem);
        printf("\033[32m[WS]\033[0m Client connected from %s\n", rem_ip);
    } else if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        char rem_ip[64];
        mg_snprintf(rem_ip, sizeof(rem_ip), "%M", mg_print_ip_port, &c->rem);
        printf("\033[36m[HTTP]\033[0m %.*s %.*s (%s)\n",
               (int)hm->method.len, hm->method.buf,
               (int)hm->uri.len, hm->uri.buf,
               rem_ip);

        if (mg_match(hm->uri, mg_str("/websocket"), NULL)) {
            mg_ws_upgrade(c, hm, NULL);
        } else {
            struct mg_http_serve_opts opts = {.root_dir = s_web_root};
            mg_http_serve_dir(c, ev_data, &opts);
        }
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
        struct mg_iobuf io = {0, 0, 0, 1024};
        struct mg_rpc_req r = {&s_rpc_head, 0, mg_pfn_iobuf, &io, 0, wm->data};
        mg_rpc_process(&r);
        if (io.buf && io.len > 0) {
            mg_ws_send(c, (char *)io.buf, io.len, WEBSOCKET_OP_TEXT);
        }
        mg_iobuf_free(&io);
    } else if (ev == MG_EV_CLOSE) {
        if (c->data[0] == 'W') {
            char rem_ip[64];
            mg_snprintf(rem_ip, sizeof(rem_ip), "%M", mg_print_ip_port, &c->rem);
            printf("\033[33m[WS]\033[0m Client disconnected (%s)\n", rem_ip);
            c->data[0] = 0;
        }
    }
}

static void resolve_web_root(void) {
    char *res = realpath(s_web_root, NULL);
    if (res != NULL) {
        struct stat st;
        char test_file[4096];
        snprintf(test_file, sizeof(test_file), "%s/index.html", res);
        if (stat(test_file, &st) == 0) {
            snprintf(s_web_root, sizeof(s_web_root), "%s", res);
            free(res);
            return;
        }
        free(res);
    }

    /* Fallback search paths */
    const char *candidates[] = {
        "../client",
        "./client",
        "Robot-Aspiradora-Yocto/client",
        "client"
    };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        res = realpath(candidates[i], NULL);
        if (res != NULL) {
            struct stat st;
            char test_file[4096];
            snprintf(test_file, sizeof(test_file), "%s/index.html", res);
            if (stat(test_file, &st) == 0) {
                snprintf(s_web_root, sizeof(s_web_root), "%s", res);
                free(res);
                return;
            }
            free(res);
        }
    }

    printf("\033[33m[WARN]\033[0m client/index.html not found, using unresolved: %s\n", s_web_root);
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    int opt;
    while ((opt = getopt(argc, argv, "p:r:d:h")) != -1) {
        switch (opt) {
            case 'p': {
                static char listen_buf[64];
                snprintf(listen_buf, sizeof(listen_buf), "http://0.0.0.0:%s", optarg);
                s_listen_on = listen_buf;
                break;
            }
            case 'r':
                snprintf(s_web_root, sizeof(s_web_root), "%s", optarg);
                break;
            case 'd':
                auth_init(optarg);
                break;
            case 'h':
            default:
                printf("Usage: %s [-p port] [-r web_root] [-d db_path]\n", argv[0]);
                return 1;
        }
    }

    resolve_web_root();

    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    mg_log_set(MG_LL_INFO);

    /* Initialize subsystems */
    auth_init("users.db");
    sim_robot_init();
    rpc_handlers_init(&s_rpc_head);

    /* Add 5 Hz simulation and telemetry timer (every 200 ms) */
    mg_timer_add(&mgr, 200, MG_TIMER_REPEAT, timer_tick, &mgr);

    printf("\n=======================================================\n");
    printf("   Robot Aspiradora Autónomo - Mongoose Web Server     \n");
    printf("=======================================================\n");
    printf(" HTTP / Web Root : %s\n", s_web_root);
    printf(" Listening on    : %s\n", s_listen_on);
    printf(" WebSocket URL   : %s/websocket\n", s_listen_on);
    printf(" Default User    : admin / admin123\n");
    printf(" Press Ctrl+C to terminate\n");
    printf("=======================================================\n\n");

    if (mg_http_listen(&mgr, s_listen_on, server_fn, NULL) == NULL) {
        fprintf(stderr, "Failed to start listener on %s\n", s_listen_on);
        return 1;
    }

    while (s_running) {
        mg_mgr_poll(&mgr, 50);
    }

    printf("\n\033[33m[SHUTDOWN]\033[0m Shutting down server gracefully...\n");
    mg_mgr_free(&mgr);
    mg_rpc_del(&s_rpc_head, NULL);
    auth_cleanup();

    printf("\033[32m[SHUTDOWN]\033[0m Clean exit.\n");
    return 0;
}
