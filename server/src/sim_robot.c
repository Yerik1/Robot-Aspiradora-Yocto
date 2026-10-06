#include "sim_robot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SENSOR_THRESHOLD_CM 18.0f
#define CELL_SIZE_CM        15.0f

static robot_state_t s_robot;

/* Hidden simulated physical room layout with walls and obstacles */
static uint8_t s_physical_room[MAP_HEIGHT][MAP_WIDTH];

/* Autonomous state machine helper variables */
typedef enum {
    AUTO_STATE_FORWARD = 0,
    AUTO_STATE_BACKING_UP,
    AUTO_STATE_TURNING
} auto_substate_t;

static auto_substate_t s_auto_state = AUTO_STATE_FORWARD;
static double s_auto_timer = 0.0;
static float s_target_turn_angle = 0.0f;

static void place_room_obstacle(int x, int y) {
    if (x >= 0 && x < MAP_WIDTH && y >= 0 && y < MAP_HEIGHT) {
        s_physical_room[y][x] = CELL_OBSTACLE;
    }
}

static void record_cell_update(int x, int y, uint8_t val) {
    if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) return;
    if (s_robot.grid[y][x] == val) return;

    s_robot.grid[y][x] = val;
    if (s_robot.update_count < sizeof(s_robot.updates) / sizeof(s_robot.updates[0])) {
        s_robot.updates[s_robot.update_count].x = x;
        s_robot.updates[s_robot.update_count].y = y;
        s_robot.updates[s_robot.update_count].val = val;
        s_robot.update_count++;
    }
}

/* Cast a ray from robot center at an angle (degrees) to find distance in cm to nearest physical obstacle */
static float cast_sensor_ray(float start_x, float start_y, float angle_deg, int *out_hit_x, int *out_hit_y) {
    float rad = (float)(angle_deg * M_PI / 180.0);
    float cos_a = cosf(rad);
    float sin_a = sinf(rad);

    float max_range_cells = 6.0f; /* ~90 cm */
    float step = 0.2f;

    for (float dist = 0.4f; dist < max_range_cells; dist += step) {
        float cx = start_x + cos_a * dist;
        float cy = start_y + sin_a * dist;
        int gx = (int)roundf(cx);
        int gy = (int)roundf(cy);

        if (gx < 0 || gx >= MAP_WIDTH || gy < 0 || gy >= MAP_HEIGHT) {
            if (out_hit_x) *out_hit_x = gx < 0 ? 0 : (gx >= MAP_WIDTH ? MAP_WIDTH - 1 : gx);
            if (out_hit_y) *out_hit_y = gy < 0 ? 0 : (gy >= MAP_HEIGHT ? MAP_HEIGHT - 1 : gy);
            return dist * CELL_SIZE_CM;
        }

        if (s_physical_room[gy][gx] == CELL_OBSTACLE) {
            if (out_hit_x) *out_hit_x = gx;
            if (out_hit_y) *out_hit_y = gy;
            return dist * CELL_SIZE_CM;
        }
    }

    if (out_hit_x) *out_hit_x = -1;
    if (out_hit_y) *out_hit_y = -1;
    return max_range_cells * CELL_SIZE_CM;
}

void sim_robot_init(void) {
    memset(&s_robot, 0, sizeof(s_robot));
    memset(s_physical_room, 0, sizeof(s_physical_room));

    /* Build outer perimeter walls */
    for (int x = 0; x < MAP_WIDTH; x++) {
        place_room_obstacle(x, 0);
        place_room_obstacle(x, MAP_HEIGHT - 1);
    }
    for (int y = 0; y < MAP_HEIGHT; y++) {
        place_room_obstacle(0, y);
        place_room_obstacle(MAP_WIDTH - 1, y);
    }

    /* Place interior furniture/obstacles */
    for (int x = 5; x <= 8; x++) place_room_obstacle(x, 6);
    for (int y = 14; y <= 17; y++) place_room_obstacle(7, y);
    for (int x = 16; x <= 19; x++) place_room_obstacle(x, 15);
    place_room_obstacle(17, 7);
    place_room_obstacle(18, 7);

    /* Initialize robot state */
    s_robot.mode = MODE_MANUAL;
    s_robot.robot_x = 12.0f;
    s_robot.robot_y = 12.0f;
    s_robot.heading_deg = 0.0f;
    snprintf(s_robot.direction, sizeof(s_robot.direction), "stop");
    s_robot.speed_percent = 50;
    s_robot.radius_percent = 50;
    s_robot.vacuum_on = false;

    /* LEDs */
    s_robot.led_autonomous = false;
    s_robot.led_manual = true;
    s_robot.led_obstacle_alert = false;
    s_robot.led_system_on = true;

    /* Audio */
    s_robot.audio_state = SIM_AUDIO_STOPPED;
    s_robot.current_track[0] = '\0';
    s_robot.volume_percent = 70;

    /* Initial sensors */
    s_robot.front_distance_cm = 80.0f;
    s_robot.left_distance_cm = 80.0f;
    s_robot.right_distance_cm = 80.0f;

    /* Mark initial position as visited */
    record_cell_update((int)roundf(s_robot.robot_x), (int)roundf(s_robot.robot_y), CELL_VISITED);

    printf("\033[32m[ROBOT]\033[0m Simulated robot engine initialized at position (%.1f, %.1f), mode=MANUAL\n",
           s_robot.robot_x, s_robot.robot_y);
}

robot_state_t *sim_robot_get_state(void) {
    return &s_robot;
}

bool sim_robot_set_mode(robot_mode_t mode) {
    s_robot.mode = mode;
    if (mode == MODE_AUTONOMOUS) {
        s_robot.led_autonomous = true;
        s_robot.led_manual = false;
        s_robot.vacuum_on = true;
        s_auto_state = AUTO_STATE_FORWARD;
        snprintf(s_robot.direction, sizeof(s_robot.direction), "forward");
        s_robot.speed_percent = 60;
        printf("\033[34m[MODE]\033[0m Switched to AUTONOMOUS mode. Suction motor turned ON.\n");
        sim_robot_audio_play_notification(NOTIF_AUTO_START);
    } else {
        s_robot.led_autonomous = false;
        s_robot.led_manual = true;
        snprintf(s_robot.direction, sizeof(s_robot.direction), "stop");
        s_robot.left_motor_speed = 0;
        s_robot.right_motor_speed = 0;
        printf("\033[34m[MODE]\033[0m Switched to MANUAL mode.\n");
        sim_robot_audio_play_notification(NOTIF_MANUAL_MODE);
    }
    return true;
}

bool sim_robot_move(const char *direction, uint8_t speed, uint8_t radius) {
    if (s_robot.mode == MODE_AUTONOMOUS) {
        printf("\033[33m[ROBOT]\033[0m Ignored manual move command while in AUTONOMOUS mode.\n");
        return false;
    }

    if (direction) {
        snprintf(s_robot.direction, sizeof(s_robot.direction), "%s", direction);
    }
    s_robot.speed_percent = speed;
    s_robot.radius_percent = radius;

    if (strcmp(s_robot.direction, "forward") == 0) {
        s_robot.left_motor_speed = speed;
        s_robot.right_motor_speed = speed;
    } else if (strcmp(s_robot.direction, "backward") == 0) {
        s_robot.left_motor_speed = speed;
        s_robot.right_motor_speed = speed;
    } else if (strcmp(s_robot.direction, "left") == 0) {
        s_robot.left_motor_speed = (uint8_t)(speed * (100 - radius) / 100);
        s_robot.right_motor_speed = speed;
    } else if (strcmp(s_robot.direction, "right") == 0) {
        s_robot.left_motor_speed = speed;
        s_robot.right_motor_speed = (uint8_t)(speed * (100 - radius) / 100);
    } else {
        s_robot.left_motor_speed = 0;
        s_robot.right_motor_speed = 0;
    }

    printf("\033[36m[TRACTION]\033[0m Direction: %s, Speed: %d%%, Radius: %d%% (L: %d, R: %d)\n",
           s_robot.direction, speed, radius, s_robot.left_motor_speed, s_robot.right_motor_speed);
    return true;
}

bool sim_robot_stop(void) {
    return sim_robot_move("stop", 0, 0);
}

bool sim_robot_set_vacuum(bool on) {
    s_robot.vacuum_on = on;
    printf("\033[36m[VACUUM]\033[0m Suction motor %s\n", on ? "STARTED" : "STOPPED");
    return true;
}

#include <stdlib.h>

/* ==========================================================================
 * Audio: dos "canales"
 *  - MÚSICA: un proceso persistente `mpg123 -R` (s_audio_pipe) controlado
 *    por comandos de texto (LOAD/PAUSE/STOP/V).
 *  - NOTIFICACIÓN: un proceso corto `mpg123 -q SoundEffects/<x>.mp3`
 *    lanzado con fork+exec (no bloquea el loop de mongoose).
 * Mientras suena una notificación la música se ATENÚA (ducking) a
 * NOTIF_DUCK_PERCENT de su volumen y se restaura cuando la notificación
 * termina (detectado con waitpid(WNOHANG) desde sim_robot_tick()).
 * ========================================================================== */

#define NOTIF_DIR            "SoundEffects"
#define NOTIF_DUCK_PERCENT   25    /* % del volumen del usuario durante la notificación */
#define NOTIF_COOLDOWN_SEC   1.0   /* tiempo mínimo entre dos notificaciones del mismo evento */

static FILE *s_audio_pipe = NULL;
static pid_t s_notif_pid = -1;          /* proceso de la notificación en curso */
static bool s_music_ducked = false;     /* música atenuada por una notificación */
static double s_last_notif_time[NOTIF_MANUAL_MODE + 1];

static const char *s_notif_files[] = {
    [NOTIF_SYSTEM_START] = NOTIF_DIR "/inicio.mp3",
    [NOTIF_AUTO_START]   = NOTIF_DIR "/modoautomatico.mp3",
    [NOTIF_OBSTACLE]     = NOTIF_DIR "/obstaculo.mp3",
    [NOTIF_MANUAL_MODE]  = NOTIF_DIR "/modomanual.mp3",
};

static double monotonic_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* Volumen que realmente debe tener el canal de música ahora mismo. */
static int music_effective_volume(void) {
    if (s_music_ducked) {
        return (int)s_robot.volume_percent * NOTIF_DUCK_PERCENT / 100;
    }
    return (int)s_robot.volume_percent;
}

static void ensure_audio_pipe(void) {
    if (!s_audio_pipe) {
        /* Si mpg123 muere, escribir al pipe no debe matar al servidor. */
        signal(SIGPIPE, SIG_IGN);
        /* Redirigir stdout y stderr a /dev/null para evitar que mpg123 contamine los logs */
        s_audio_pipe = popen("mpg123 -R >/dev/null 2>&1", "w");
        if (s_audio_pipe) {
            /* Que los procesos de notificación (fork+exec) no hereden el pipe */
            fcntl(fileno(s_audio_pipe), F_SETFD, FD_CLOEXEC);
            /* Initial volume setting */
            fprintf(s_audio_pipe, "V %d\n", music_effective_volume());
            fflush(s_audio_pipe);
        }
    }
}

static void music_send_volume(void) {
    ensure_audio_pipe();
    if (s_audio_pipe) {
        fprintf(s_audio_pipe, "V %d\n", music_effective_volume());
        fflush(s_audio_pipe);
    }
}


bool sim_robot_audio_play(const char *track) {
    s_robot.audio_state = SIM_AUDIO_PLAYING;
    if (track && strlen(track) > 0 && track != s_robot.current_track) {
        snprintf(s_robot.current_track, sizeof(s_robot.current_track), "%s", track);
    }
    printf("\033[35m[AUDIO]\033[0m Playing: %s (vol: %d%%)\n", s_robot.current_track, s_robot.volume_percent);
    
    ensure_audio_pipe();
    if (s_audio_pipe) {
        fprintf(s_audio_pipe, "LOAD music/%s\n", s_robot.current_track);
        fflush(s_audio_pipe);
    }
    return true;
}

bool sim_robot_audio_pause(void) {
    if (s_robot.audio_state == SIM_AUDIO_PLAYING) {
        s_robot.audio_state = SIM_AUDIO_PAUSED;
        printf("\033[35m[AUDIO]\033[0m Paused playback.\n");
        ensure_audio_pipe();
        if (s_audio_pipe) {
            fprintf(s_audio_pipe, "PAUSE\n");
            fflush(s_audio_pipe);
        }
        return true;
    }
    return false;
}

bool sim_robot_audio_resume(void) {
    if (s_robot.audio_state == SIM_AUDIO_PAUSED) {
        s_robot.audio_state = SIM_AUDIO_PLAYING;
        printf("\033[35m[AUDIO]\033[0m Resumed playback.\n");
        ensure_audio_pipe();
        if (s_audio_pipe) {
            fprintf(s_audio_pipe, "PAUSE\n"); /* PAUSE in mpg123 -R toggles pause state */
            fflush(s_audio_pipe);
        }
        return true;
    } else if (s_robot.audio_state == SIM_AUDIO_STOPPED) {
        /* If stopped, resume acts like play for convenience */
        return sim_robot_audio_play(s_robot.current_track);
    }
    return false;
}

bool sim_robot_audio_stop(void) {
    s_robot.audio_state = SIM_AUDIO_STOPPED;
    printf("\033[35m[AUDIO]\033[0m Stopped audio playback.\n");
    ensure_audio_pipe();
    if (s_audio_pipe) {
        fprintf(s_audio_pipe, "STOP\n");
        fflush(s_audio_pipe);
    }
    return true;
}

bool sim_robot_audio_set_volume(uint8_t volume) {
    if (volume > 100) volume = 100;
    s_robot.volume_percent = volume;
    printf("\033[35m[AUDIO]\033[0m Volume set to %d%%%s\n", volume,
           s_music_ducked ? " (applied after notification)" : "");

    /* Si hay una notificación sonando, la música sigue atenuada respecto
     * al nuevo volumen; se restaurará completo al terminar. */
    music_send_volume();

    return true;
}

/* Termina y recoge el proceso de notificación en curso (si existe). */
static void notif_kill_current(void) {
    if (s_notif_pid > 0) {
        kill(s_notif_pid, SIGTERM);
        waitpid(s_notif_pid, NULL, 0);
        s_notif_pid = -1;
    }
}

/* Lanza `mpg123 -q <file>` como proceso hijo sin bloquear. */
static pid_t notif_spawn(const char *file) {
    pid_t pid = fork();
    if (pid != 0) return pid; /* padre (o -1 si fork falló) */

    /* Hijo: silenciar E/S y no heredar sockets de mongoose */
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > STDERR_FILENO) close(devnull);
    }
    for (int fd = 3; fd < 1024; fd++) close(fd);

    execlp("mpg123", "mpg123", "-q", file, (char *)NULL);
    _exit(127); /* execlp falló */
}

bool sim_robot_audio_play_notification(notif_event_t event) {
    const char *names[] = {
        "System Startup (Task 3.3)",
        "Autonomous Mode Start",
        "Obstacle Detected Alert",
        "Switch to Manual Mode"
    };
    if ((int)event < NOTIF_SYSTEM_START || event > NOTIF_MANUAL_MODE) {
        return false;
    }

    /* Cooldown por evento: evita repetir la alerta de obstáculo sin parar
     * mientras el robot rebota. */
    double now = monotonic_now();
    if (s_last_notif_time[event] > 0.0 &&
        now - s_last_notif_time[event] < NOTIF_COOLDOWN_SEC) {
        printf("\033[35m[AUDIO NOTIF]\033[0m Skipped (cooldown): %s\n", names[event]);
        return false;
    }

    const char *file = s_notif_files[event];
    if (access(file, R_OK) != 0) {
        printf("\033[31m[AUDIO NOTIF]\033[0m File not found: %s (run the server from server/)\n", file);
        return false;
    }
    s_last_notif_time[event] = now;

    /* La notificación más reciente gana: se corta la anterior (la música
     * permanece atenuada, no hay "salto" de volumen entre ambas). */
    notif_kill_current();

    /* Atenuar la música solo si realmente está sonando */
    if (s_robot.audio_state == SIM_AUDIO_PLAYING && !s_music_ducked) {
        s_music_ducked = true;
        music_send_volume();
    }

    s_notif_pid = notif_spawn(file);
    if (s_notif_pid < 0) {
        printf("\033[31m[AUDIO NOTIF]\033[0m fork() failed: %s\n", strerror(errno));
        sim_robot_audio_service(); /* restaura el volumen de la música */
        return false;
    }

    printf("\033[35m[AUDIO NOTIF]\033[0m Playing notification: %s (%s)%s\n",
           names[event], file, s_music_ducked ? " - music ducked" : "");
    return true;
}

void sim_robot_audio_service(void) {
    if (s_notif_pid > 0) {
        pid_t r = waitpid(s_notif_pid, NULL, WNOHANG);
        if (r == 0) return;           /* la notificación sigue sonando */
        s_notif_pid = -1;             /* terminó (r == pid) o ya no existe (r < 0) */
    }
    if (s_music_ducked) {
        s_music_ducked = false;
        music_send_volume();
        printf("\033[35m[AUDIO NOTIF]\033[0m Notification finished - music volume restored to %d%%\n",
               s_robot.volume_percent);
    }
}

void sim_robot_audio_shutdown(void) {
    notif_kill_current();
    s_music_ducked = false;
    if (s_audio_pipe) {
        fprintf(s_audio_pipe, "QUIT\n");
        fflush(s_audio_pipe);
        pclose(s_audio_pipe);
        s_audio_pipe = NULL;
    }
}

void sim_robot_map_reset(void) {
    memset(s_robot.grid, 0, sizeof(s_robot.grid));
    s_robot.update_count = 0;
    record_cell_update((int)roundf(s_robot.robot_x), (int)roundf(s_robot.robot_y), CELL_VISITED);
    printf("\033[32m[MAP]\033[0m Explored route map cleared.\n");
}

void sim_robot_tick(double dt_seconds) {
    /* ¿Terminó la notificación en curso? -> restaurar volumen de la música */
    sim_robot_audio_service();

    /* Reset update count for this tick */
    s_robot.update_count = 0;

    /* Update proximity sensor readings */
    int hit_fx = -1, hit_fy = -1;
    int hit_lx = -1, hit_ly = -1;
    int hit_rx = -1, hit_ry = -1;

    s_robot.front_distance_cm = cast_sensor_ray(s_robot.robot_x, s_robot.robot_y, s_robot.heading_deg, &hit_fx, &hit_fy);
    s_robot.left_distance_cm  = cast_sensor_ray(s_robot.robot_x, s_robot.robot_y, s_robot.heading_deg - 70.0f, &hit_lx, &hit_ly);
    s_robot.right_distance_cm = cast_sensor_ray(s_robot.robot_x, s_robot.robot_y, s_robot.heading_deg + 70.0f, &hit_rx, &hit_ry);

    s_robot.front_obstacle = (s_robot.front_distance_cm <= SENSOR_THRESHOLD_CM);
    s_robot.left_obstacle  = (s_robot.left_distance_cm <= SENSOR_THRESHOLD_CM);
    s_robot.right_obstacle = (s_robot.right_distance_cm <= SENSOR_THRESHOLD_CM);

    bool any_obstacle = s_robot.front_obstacle || s_robot.left_obstacle || s_robot.right_obstacle;
    s_robot.led_obstacle_alert = any_obstacle;

    /* If obstacle is detected in front, record it on the route map */
    if (s_robot.front_obstacle && hit_fx >= 0 && hit_fy >= 0) {
        record_cell_update(hit_fx, hit_fy, CELL_OBSTACLE);
    }
    if (s_robot.left_obstacle && hit_lx >= 0 && hit_ly >= 0) {
        record_cell_update(hit_lx, hit_ly, CELL_OBSTACLE);
    }
    if (s_robot.right_obstacle && hit_rx >= 0 && hit_ry >= 0) {
        record_cell_update(hit_rx, hit_ry, CELL_OBSTACLE);
    }

    /* Autonomous Mode State Machine (Reactive bounce navigation) */
    if (s_robot.mode == MODE_AUTONOMOUS) {
        float speed_scale = (float)s_robot.speed_percent / 100.0f;
        float move_speed_cells_per_sec = 1.5f * speed_scale;

        switch (s_auto_state) {
            case AUTO_STATE_FORWARD:
                if (s_robot.front_obstacle) {
                    printf("\033[31m[AUTONOMOUS]\033[0m Obstacle detected! (Dist: %.1f cm). Stopping and backing up.\n",
                           s_robot.front_distance_cm);
                    sim_robot_audio_play_notification(NOTIF_OBSTACLE);
                    s_auto_state = AUTO_STATE_BACKING_UP;
                    s_auto_timer = 0.8; /* Back up for 0.8s */
                    snprintf(s_robot.direction, sizeof(s_robot.direction), "backward");
                } else {
                    /* Advance forward */
                    float rad = (float)(s_robot.heading_deg * M_PI / 180.0);
                    float nx = s_robot.robot_x + cosf(rad) * move_speed_cells_per_sec * (float)dt_seconds;
                    float ny = s_robot.robot_y + sinf(rad) * move_speed_cells_per_sec * (float)dt_seconds;

                    int gx = (int)roundf(nx);
                    int gy = (int)roundf(ny);

                    if (gx > 0 && gx < MAP_WIDTH - 1 && gy > 0 && gy < MAP_HEIGHT - 1 &&
                        s_physical_room[gy][gx] != CELL_OBSTACLE) {
                        s_robot.robot_x = nx;
                        s_robot.robot_y = ny;
                        record_cell_update(gx, gy, CELL_VISITED);
                    } else {
                        /* Bumped into obstacle */
                        s_auto_state = AUTO_STATE_BACKING_UP;
                        s_auto_timer = 0.8;
                        snprintf(s_robot.direction, sizeof(s_robot.direction), "backward");
                    }
                }
                break;

            case AUTO_STATE_BACKING_UP:
                s_auto_timer -= dt_seconds;
                {
                    float rad = (float)(s_robot.heading_deg * M_PI / 180.0);
                    float nx = s_robot.robot_x - cosf(rad) * (move_speed_cells_per_sec * 0.8f) * (float)dt_seconds;
                    float ny = s_robot.robot_y - sinf(rad) * (move_speed_cells_per_sec * 0.8f) * (float)dt_seconds;
                    int gx = (int)roundf(nx);
                    int gy = (int)roundf(ny);
                    if (gx > 0 && gx < MAP_WIDTH - 1 && gy > 0 && gy < MAP_HEIGHT - 1 &&
                        s_physical_room[gy][gx] != CELL_OBSTACLE) {
                        s_robot.robot_x = nx;
                        s_robot.robot_y = ny;
                    }
                }
                if (s_auto_timer <= 0.0) {
                    s_auto_state = AUTO_STATE_TURNING;
                    s_auto_timer = 1.0; /* Turn duration */
                    /* Pick turn angle: 90 to 140 degrees */
                    s_target_turn_angle = (rand() % 2 == 0 ? 1.0f : -1.0f) * (90.0f + (rand() % 50));
                    snprintf(s_robot.direction, sizeof(s_robot.direction), s_target_turn_angle > 0 ? "right" : "left");
                }
                break;

            case AUTO_STATE_TURNING:
                s_auto_timer -= dt_seconds;
                s_robot.heading_deg += s_target_turn_angle * (float)dt_seconds;
                if (s_robot.heading_deg >= 360.0f) s_robot.heading_deg -= 360.0f;
                if (s_robot.heading_deg < 0.0f) s_robot.heading_deg += 360.0f;

                if (s_auto_timer <= 0.0) {
                    s_auto_state = AUTO_STATE_FORWARD;
                    snprintf(s_robot.direction, sizeof(s_robot.direction), "forward");
                }
                break;
        }
    } else {
        /* Manual Mode Motion */
        float speed_scale = (float)s_robot.speed_percent / 100.0f;
        float move_speed = 2.0f * speed_scale * (float)dt_seconds;
        float rot_speed = 90.0f * speed_scale * (float)dt_seconds;

        if (strcmp(s_robot.direction, "forward") == 0) {
            float rad = (float)(s_robot.heading_deg * M_PI / 180.0);
            float nx = s_robot.robot_x + cosf(rad) * move_speed;
            float ny = s_robot.robot_y + sinf(rad) * move_speed;
            int gx = (int)roundf(nx);
            int gy = (int)roundf(ny);

            if (gx > 0 && gx < MAP_WIDTH - 1 && gy > 0 && gy < MAP_HEIGHT - 1 &&
                s_physical_room[gy][gx] != CELL_OBSTACLE) {
                s_robot.robot_x = nx;
                s_robot.robot_y = ny;
                record_cell_update(gx, gy, CELL_VISITED);
            } else {
                s_robot.front_obstacle = true;
                s_robot.led_obstacle_alert = true;
                if (gx >= 0 && gx < MAP_WIDTH && gy >= 0 && gy < MAP_HEIGHT) {
                    record_cell_update(gx, gy, CELL_OBSTACLE);
                }
            }
        } else if (strcmp(s_robot.direction, "backward") == 0) {
            float rad = (float)(s_robot.heading_deg * M_PI / 180.0);
            float nx = s_robot.robot_x - cosf(rad) * move_speed;
            float ny = s_robot.robot_y - sinf(rad) * move_speed;
            int gx = (int)roundf(nx);
            int gy = (int)roundf(ny);

            if (gx > 0 && gx < MAP_WIDTH - 1 && gy > 0 && gy < MAP_HEIGHT - 1 &&
                s_physical_room[gy][gx] != CELL_OBSTACLE) {
                s_robot.robot_x = nx;
                s_robot.robot_y = ny;
                record_cell_update(gx, gy, CELL_VISITED);
            }
        } else if (strcmp(s_robot.direction, "left") == 0) {
            s_robot.heading_deg -= rot_speed;
            if (s_robot.heading_deg < 0.0f) s_robot.heading_deg += 360.0f;
        } else if (strcmp(s_robot.direction, "right") == 0) {
            s_robot.heading_deg += rot_speed;
            if (s_robot.heading_deg >= 360.0f) s_robot.heading_deg -= 360.0f;
        }
    }
}
