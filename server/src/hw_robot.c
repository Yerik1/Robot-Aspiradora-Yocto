/**
 * hw_robot.c
 *
 * Capa entre el servidor web y librobot.so.
 *
 * Hilos:
 *   - Hilo de sensores (propio de este archivo): es el UNICO que llama a
 *     funciones sensor_*() de librobot. La lectura del HC-SR04 bloquea
 *     hasta ~35 ms mas la separacion minima entre disparos, por eso no se
 *     hace en el bucle de Mongoose. Guarda la ultima lectura en
 *     s_sensors, protegida con s_sensor_lock.
 *   - Hilo principal (Mongoose): motores, aspirado, LEDs y audio. Lee la
 *     ultima lectura de sensores en hw_robot_tick() cada 200 ms.
 *
 * Comportamiento autonomo (especificacion del proyecto): al detectar un
 * obstaculo el robot se detiene, reproduce la notificacion de obstaculo,
 * enciende el LED de obstaculo, retrocede y cambia de direccion. Si el
 * obstaculo esta a un costado, gira hacia el lado contrario.
 */
#include "hw_robot.h"
#include <librobot.h>

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- Sensores ----------------------------------------------------------- */
#define FRONT_THRESHOLD_CM   18.0f   /* obstaculo frontal si distancia <= umbral */
#define FRONT_MAX_RANGE_CM  400.0f   /* valor reportado cuando no hay eco        */
#define IR_RANGE_CM           1.5f   /* alcance util del TCRT5000L (~15 mm)      */
#define SENSOR_PERIOD_MS    100      /* periodo del hilo de sensores             */

/* ---- Mapa y odometria (sin encoders: estimacion por tiempo) ------------- */
#define CELL_SIZE_CM         15.0f
/* CALIBRAR EN HARDWARE: celdas por segundo a velocidad 100 % */
#define AUTO_CELLS_PER_S      1.5f
#define MANUAL_CELLS_PER_S    2.0f
/* CALIBRAR EN HARDWARE: grados por segundo girando sobre el eje a 100 % */
#define MANUAL_DEG_PER_S     90.0f

/* ---- Navegacion autonoma ------------------------------------------------ */
#define AUTO_SPEED_PERCENT   60
#define AUTO_STOP_S           0.3    /* pausa detenido antes de retroceder */
#define AUTO_BACKUP_S         0.8    /* tiempo retrocediendo               */
/* CALIBRAR EN HARDWARE: tiempo de giro para cambiar de direccion */
#define AUTO_TURN_S           1.0

/* ---- Audio -------------------------------------------------------------- */
#define MUSIC_DIR "/usr/share/robot-aspirador/music"   /* mismo que librobot */

static robot_state_t s_robot;

/* ========================================================================= */
/* Hilo de sensores                                                          */
/* ========================================================================= */

typedef struct {
    float front_cm;
    bool front_obs;
    bool left_obs;
    bool right_obs;
} sensor_snapshot_t;

static pthread_mutex_t s_sensor_lock = PTHREAD_MUTEX_INITIALIZER;
static sensor_snapshot_t s_sensors = { FRONT_MAX_RANGE_CM, false, false, false };
static pthread_t s_sensor_thread;
static atomic_bool s_sensor_run = false;
static bool s_sensor_thread_started = false;
static bool s_hw_initialized = false;

static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void *sensor_thread_fn(void *arg)
{
    (void)arg;
    while (atomic_load(&s_sensor_run)) {
        sensor_snapshot_t snap;

        float cm = 0.0f;
        robot_status_t st = sensor_read_distance_cm(SENSOR_FRONT, &cm);
        if (st == ROBOT_OK) {
            snap.front_cm = cm;
            snap.front_obs = (cm <= FRONT_THRESHOLD_CM);
        } else {
            /* ROBOT_ERR_SENSOR_TIMEOUT = sin eco: nada al alcance. Cualquier
             * otro error tambien se trata como "sin obstaculo" para no
             * bloquear la navegacion por una lectura fallida. */
            snap.front_cm = FRONT_MAX_RANGE_CM;
            snap.front_obs = false;
        }

        bool l = false;
        bool r = false;
        if (sensor_read_obstacle(SENSOR_LEFT, 0.0f, &l) != ROBOT_OK)  l = false;
        if (sensor_read_obstacle(SENSOR_RIGHT, 0.0f, &r) != ROBOT_OK) r = false;
        snap.left_obs = l;
        snap.right_obs = r;

        pthread_mutex_lock(&s_sensor_lock);
        s_sensors = snap;
        pthread_mutex_unlock(&s_sensor_lock);

        sleep_ms(SENSOR_PERIOD_MS);
    }
    return NULL;
}

/* ========================================================================= */
/* Utilidades                                                                */
/* ========================================================================= */

typedef enum {
    AUTO_STATE_FORWARD = 0,
    AUTO_STATE_STOPPING,
    AUTO_STATE_BACKING_UP,
    AUTO_STATE_TURNING
} auto_substate_t;

static auto_substate_t s_auto_state = AUTO_STATE_FORWARD;
static double s_auto_timer = 0.0;
static float s_target_turn_angle = 0.0f;
static int s_turn_dir = 1;   /* +1 = derecha, -1 = izquierda */

static void record_cell_update(int x, int y, uint8_t val)
{
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

static void set_direction(const char *dir)
{
    snprintf(s_robot.direction, sizeof(s_robot.direction), "%s", dir);
}

/* Velocidad de cada motor para la telemetria, estimada a partir del comando
 * (librobot no expone la velocidad real de cada motor). */
static void set_motor_telemetry(const char *dir, uint8_t speed, uint8_t radius)
{
    uint8_t inner = (uint8_t)((unsigned)speed * radius / 100u);
    if (strcmp(dir, "left") == 0) {
        s_robot.left_motor_speed = (radius == 0) ? speed : inner;
        s_robot.right_motor_speed = speed;
    } else if (strcmp(dir, "right") == 0) {
        s_robot.left_motor_speed = speed;
        s_robot.right_motor_speed = (radius == 0) ? speed : inner;
    } else if (strcmp(dir, "forward") == 0 || strcmp(dir, "backward") == 0) {
        s_robot.left_motor_speed = speed;
        s_robot.right_motor_speed = speed;
    } else {
        s_robot.left_motor_speed = 0;
        s_robot.right_motor_speed = 0;
    }
}

static void drive(const char *dir, uint8_t speed, uint8_t radius)
{
    set_direction(dir);
    set_motor_telemetry(dir, speed, radius);

    robot_status_t st;
    if (strcmp(dir, "forward") == 0) {
        st = robot_move_forward(speed);
    } else if (strcmp(dir, "backward") == 0) {
        st = robot_move_backward(speed);
    } else if (strcmp(dir, "left") == 0) {
        st = robot_turn_left(speed, radius);
    } else if (strcmp(dir, "right") == 0) {
        st = robot_turn_right(speed, radius);
    } else {
        st = motor_stop_all();
    }
    if (st != ROBOT_OK) {
        printf("\033[31m[ROBOT]\033[0m Motor command '%s' failed (%d)\n", dir, st);
    }
}

static void set_led(led_id_t led, bool *field, bool on)
{
    *field = on;
    led_set(led, on);
}

/* Intenta mover la posicion estimada; solo avanza si la celda destino es
 * valida y no es un obstaculo conocido. */
static void try_move(float dx, float dy)
{
    float nx = s_robot.robot_x + dx;
    float ny = s_robot.robot_y + dy;
    int gx = (int)roundf(nx);
    int gy = (int)roundf(ny);
    if (gx > 0 && gx < MAP_WIDTH - 1 && gy > 0 && gy < MAP_HEIGHT - 1 &&
        s_robot.grid[gy][gx] != CELL_OBSTACLE) {
        s_robot.robot_x = nx;
        s_robot.robot_y = ny;
        record_cell_update(gx, gy, CELL_VISITED);
    }
}

static void rotate(float deg)
{
    s_robot.heading_deg += deg;
    while (s_robot.heading_deg >= 360.0f) s_robot.heading_deg -= 360.0f;
    while (s_robot.heading_deg < 0.0f)    s_robot.heading_deg += 360.0f;
}

/* ========================================================================= */
/* Inicializacion / cierre                                                   */
/* ========================================================================= */

bool hw_robot_init(void)
{
    memset(&s_robot, 0, sizeof(s_robot));

    robot_status_t st = robot_init();
    if (st != ROBOT_OK) {
        fprintf(stderr, "\033[31m[ROBOT]\033[0m robot_init() failed (%d)\n", st);
        return false;
    }
    s_hw_initialized = true;
    srand((unsigned)time(NULL));

    s_robot.mode = MODE_MANUAL;
    printf("\033[32m[VERSION]\033[0m Demo 1 v2: aspiradora en autonomo = GPIO26 HIGH\n");
    s_robot.robot_x = 12.0f;
    s_robot.robot_y = 12.0f;
    s_robot.heading_deg = 0.0f;
    set_direction("stop");
    s_robot.speed_percent = 50;
    s_robot.radius_percent = 50;

    uint8_t vol = 0;
    if (audio_get_volume(&vol) == ROBOT_OK) {
        s_robot.volume_percent = vol;
    }

    s_robot.front_distance_cm = FRONT_MAX_RANGE_CM;
    s_robot.left_distance_cm = -1.0f;
    s_robot.right_distance_cm = -1.0f;

    /* LEDs fisicos: sistema encendido + modo manual */
    set_led(LED_SYSTEM_ON, &s_robot.led_system_on, true);
    set_led(LED_MANUAL_MODE, &s_robot.led_manual, true);
    set_led(LED_AUTONOMOUS_MODE, &s_robot.led_autonomous, false);
    set_led(LED_OBSTACLE_ALERT, &s_robot.led_obstacle_alert, false);

    atomic_store(&s_sensor_run, true);
    if (pthread_create(&s_sensor_thread, NULL, sensor_thread_fn, NULL) != 0) {
        fprintf(stderr, "\033[31m[ROBOT]\033[0m Could not start sensor thread\n");
        atomic_store(&s_sensor_run, false);
        robot_cleanup();
        s_hw_initialized = false;
        return false;
    }
    s_sensor_thread_started = true;

    record_cell_update((int)roundf(s_robot.robot_x), (int)roundf(s_robot.robot_y), CELL_VISITED);

    printf("\033[32m[ROBOT]\033[0m Hardware initialized at (%.1f, %.1f), mode=MANUAL\n",
           s_robot.robot_x, s_robot.robot_y);
    return true;
}

void hw_robot_shutdown(void)
{
    if (s_sensor_thread_started) {
        atomic_store(&s_sensor_run, false);
        pthread_join(s_sensor_thread, NULL);
        s_sensor_thread_started = false;
    }
    if (s_hw_initialized) {
        motor_stop_all();
        vacuum_off();
        audio_stop();
        robot_cleanup();   /* apaga LEDs, libera GPIO y cierra mpg123 */
        s_hw_initialized = false;
        printf("\033[32m[ROBOT]\033[0m Hardware released.\n");
    }
}

robot_state_t *hw_robot_get_state(void)
{
    audio_state_t a_state;
    if (audio_get_state(&a_state) == ROBOT_OK) {
        s_robot.audio_state = (sim_audio_state_t)a_state;   /* mismos valores */
    }
    return &s_robot;
}

/* ========================================================================= */
/* Control                                                                   */
/* ========================================================================= */

bool hw_robot_set_mode(robot_mode_t mode)
{
    s_robot.mode = mode;
    if (mode == MODE_AUTONOMOUS) {
        set_led(LED_AUTONOMOUS_MODE, &s_robot.led_autonomous, true);
        set_led(LED_MANUAL_MODE, &s_robot.led_manual, false);

        hw_robot_set_vacuum_manual(true);
        s_robot.speed_percent = AUTO_SPEED_PERCENT;
        s_robot.radius_percent = 0;
        s_auto_state = AUTO_STATE_FORWARD;
        drive("forward", AUTO_SPEED_PERCENT, 0);

        printf("\033[34m[MODE]\033[0m AUTONOMOUS. Suction motor ON.\n");
        hw_robot_audio_play_notification(NOTIF_AUTO_START);
    } else {
        drive("stop", 0, 0);
        hw_robot_set_vacuum_manual(false);

        set_led(LED_AUTONOMOUS_MODE, &s_robot.led_autonomous, false);
        set_led(LED_MANUAL_MODE, &s_robot.led_manual, true);

        printf("\033[34m[MODE]\033[0m MANUAL.\n");
        hw_robot_audio_play_notification(NOTIF_MANUAL_MODE);
    }
    return true;
}

bool hw_robot_move(const char *direction, uint8_t speed, uint8_t radius)
{
    if (s_robot.mode == MODE_AUTONOMOUS) {
        printf("\033[33m[ROBOT]\033[0m Ignored manual move while in AUTONOMOUS mode.\n");
        return false;
    }
    if (direction == NULL) {
        return false;
    }

    s_robot.speed_percent = speed;
    s_robot.radius_percent = radius;

    if (strcmp(direction, "forward") == 0 || strcmp(direction, "backward") == 0 ||
        strcmp(direction, "left") == 0 || strcmp(direction, "right") == 0) {
        drive(direction, speed, radius);
    } else {
        drive("stop", 0, 0);
    }

    printf("\033[36m[TRACTION]\033[0m Direction: %s, Speed: %d%%, Radius: %d%%\n",
           s_robot.direction, speed, radius);
    return true;
}

bool hw_robot_stop(void)
{
    drive("stop", 0, 0);
    return true;
}

bool hw_robot_set_vacuum(bool on)
{
    robot_status_t st = on ? vacuum_on() : vacuum_off();
    if (st == ROBOT_OK) {
        s_robot.vacuum_on = on;
    }
    return st == ROBOT_OK;
}

/* Aspiradora: logica que funciono en la prueba (boton manual).
 * Encendida = vacuum_off() = GPIO26 HIGH. Apagada = vacuum_on() = GPIO26 LOW. */
bool hw_robot_set_vacuum_manual(bool on)
{
    robot_status_t st = on ? vacuum_off() : vacuum_on();
    if (st == ROBOT_OK) {
        s_robot.vacuum_on = on;
        printf("\033[33m[VACUUM]\033[0m %s -> GPIO26 %s\n", on ? "ON" : "OFF", on ? "HIGH" : "LOW");
    }
    return st == ROBOT_OK;
}


/* ========================================================================= */
/* Audio                                                                     */
/* ========================================================================= */

/* Solo se aceptan nombres simples (sin rutas) para no salir de MUSIC_DIR. */
static bool valid_track_name(const char *track)
{
    if (track == NULL || track[0] == '\0') return false;
    if (strchr(track, '/') != NULL || strstr(track, "..") != NULL) return false;
    return strlen(track) < TRACK_NAME_MAX;
}

bool hw_robot_audio_play(const char *track)
{
    if (!valid_track_name(track)) {
        return false;
    }
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", MUSIC_DIR, track);

    robot_status_t st = audio_play_file(full_path);
    if (st != ROBOT_OK) {
        printf("\033[31m[AUDIO]\033[0m Could not play %s (%d)\n", full_path, st);
        return false;
    }
    snprintf(s_robot.current_track, sizeof(s_robot.current_track), "%s", track);
    return true;
}

bool hw_robot_audio_pause(void)
{
    return audio_pause() == ROBOT_OK;
}

bool hw_robot_audio_resume(void)
{
    return audio_resume() == ROBOT_OK;
}

bool hw_robot_audio_stop(void)
{
    s_robot.current_track[0] = '\0';
    return audio_stop() == ROBOT_OK;
}

bool hw_robot_audio_set_volume(uint8_t volume)
{
    if (audio_set_volume(volume) != ROBOT_OK) {
        return false;
    }
    s_robot.volume_percent = volume;
    return true;
}

bool hw_robot_audio_play_notification(notif_event_t event)
{
    /* notif_event_t y audio_event_t tienen los mismos valores (0-3).
     * Nota: librobot usa un solo reproductor, asi que la notificacion
     * reemplaza la cancion que estuviera sonando. */
    robot_status_t st = audio_play_notification((audio_event_t)event);
    if (st != ROBOT_OK) {
        printf("\033[33m[AUDIO]\033[0m Notification %d not played (%d)\n", (int)event, st);
        return false;
    }
    s_robot.current_track[0] = '\0';
    return true;
}

size_t hw_robot_audio_list(char names[][TRACK_NAME_MAX], size_t max_count)
{
    static char paths[TRACK_LIST_MAX][256];
    uint32_t found = 0;

    if (max_count > TRACK_LIST_MAX) max_count = TRACK_LIST_MAX;
    if (max_count == 0) return 0;

    if (audio_list_files(paths, (uint32_t)max_count, &found) != ROBOT_OK) {
        return 0;
    }

    size_t n = 0;
    for (uint32_t i = 0; i < found; i++) {
        const char *base = strrchr(paths[i], '/');
        base = base ? base + 1 : paths[i];
        if (valid_track_name(base)) {   /* garantiza strlen < TRACK_NAME_MAX */
            memcpy(names[n], base, strlen(base) + 1);
            n++;
        }
    }
    return n;
}

/* ========================================================================= */
/* Mapa                                                                      */
/* ========================================================================= */

void hw_robot_map_reset(void)
{
    memset(s_robot.grid, 0, sizeof(s_robot.grid));
    s_robot.update_count = 0;
    record_cell_update((int)roundf(s_robot.robot_x), (int)roundf(s_robot.robot_y), CELL_VISITED);
    printf("\033[32m[MAP]\033[0m Explored route map cleared.\n");
}

/* ========================================================================= */
/* Ciclo de 200 ms                                                           */
/* ========================================================================= */

static void update_sensors_and_map(void)
{
    sensor_snapshot_t snap;
    pthread_mutex_lock(&s_sensor_lock);
    snap = s_sensors;
    pthread_mutex_unlock(&s_sensor_lock);

    s_robot.front_distance_cm = snap.front_cm;
    s_robot.front_obstacle = snap.front_obs;
    s_robot.left_obstacle = snap.left_obs;
    s_robot.right_obstacle = snap.right_obs;
    s_robot.left_distance_cm = snap.left_obs ? IR_RANGE_CM : -1.0f;
    s_robot.right_distance_cm = snap.right_obs ? IR_RANGE_CM : -1.0f;

    bool any = snap.front_obs || snap.left_obs || snap.right_obs;
    if (any != s_robot.led_obstacle_alert) {
        set_led(LED_OBSTACLE_ALERT, &s_robot.led_obstacle_alert, any);
    }

    /* Obstaculos en el mapa segun el rumbo estimado */
    float rad = (float)(s_robot.heading_deg * M_PI / 180.0);
    if (snap.front_obs) {
        float cells = snap.front_cm / CELL_SIZE_CM;
        if (cells < 1.0f) cells = 1.0f;
        record_cell_update((int)roundf(s_robot.robot_x + cosf(rad) * cells),
                           (int)roundf(s_robot.robot_y + sinf(rad) * cells),
                           CELL_OBSTACLE);
    }
    /* Laterales a 9:00 y 3:00; su alcance es menor a una celda, se marca la
     * celda vecina. */
    if (snap.left_obs) {
        float r = (float)((s_robot.heading_deg - 90.0f) * M_PI / 180.0);
        record_cell_update((int)roundf(s_robot.robot_x + cosf(r)),
                           (int)roundf(s_robot.robot_y + sinf(r)), CELL_OBSTACLE);
    }
    if (snap.right_obs) {
        float r = (float)((s_robot.heading_deg + 90.0f) * M_PI / 180.0);
        record_cell_update((int)roundf(s_robot.robot_x + cosf(r)),
                           (int)roundf(s_robot.robot_y + sinf(r)), CELL_OBSTACLE);
    }
}

static void autonomous_step(double dt)
{
    float rad = (float)(s_robot.heading_deg * M_PI / 180.0);
    float v = AUTO_CELLS_PER_S * (float)s_robot.speed_percent / 100.0f;

    switch (s_auto_state) {
    case AUTO_STATE_FORWARD:
        if (s_robot.front_obstacle || s_robot.left_obstacle || s_robot.right_obstacle) {
            /* Girar hacia el lado libre; si no se sabe, al azar */
            if (s_robot.left_obstacle && !s_robot.right_obstacle) {
                s_turn_dir = 1;
            } else if (s_robot.right_obstacle && !s_robot.left_obstacle) {
                s_turn_dir = -1;
            } else {
                s_turn_dir = (rand() % 2 == 0) ? 1 : -1;
            }
            printf("\033[31m[AUTONOMOUS]\033[0m Obstacle (front=%s %.1f cm, left=%s, right=%s). Stopping.\n",
                   s_robot.front_obstacle ? "yes" : "no", s_robot.front_distance_cm,
                   s_robot.left_obstacle ? "yes" : "no",
                   s_robot.right_obstacle ? "yes" : "no");
            drive("stop", 0, 0);
            hw_robot_audio_play_notification(NOTIF_OBSTACLE);
            s_auto_state = AUTO_STATE_STOPPING;
            s_auto_timer = AUTO_STOP_S;
        } else {
            try_move(cosf(rad) * v * (float)dt, sinf(rad) * v * (float)dt);
        }
        break;

    case AUTO_STATE_STOPPING:
        s_auto_timer -= dt;
        if (s_auto_timer <= 0.0) {
            drive("backward", s_robot.speed_percent, 0);
            s_auto_state = AUTO_STATE_BACKING_UP;
            s_auto_timer = AUTO_BACKUP_S;
        }
        break;

    case AUTO_STATE_BACKING_UP:
        s_auto_timer -= dt;
        try_move(-cosf(rad) * v * 0.8f * (float)dt, -sinf(rad) * v * 0.8f * (float)dt);
        if (s_auto_timer <= 0.0) {
            s_target_turn_angle = (float)s_turn_dir * (90.0f + (float)(rand() % 50));
            drive(s_turn_dir > 0 ? "right" : "left", s_robot.speed_percent, 0);
            s_auto_state = AUTO_STATE_TURNING;
            s_auto_timer = AUTO_TURN_S;
        }
        break;

    case AUTO_STATE_TURNING:
        s_auto_timer -= dt;
        rotate(s_target_turn_angle / (float)AUTO_TURN_S * (float)dt);
        if (s_auto_timer <= 0.0) {
            drive("forward", s_robot.speed_percent, 0);
            s_auto_state = AUTO_STATE_FORWARD;
        }
        break;
    }
}

static void manual_step(double dt)
{
    float rad = (float)(s_robot.heading_deg * M_PI / 180.0);
    float scale = (float)s_robot.speed_percent / 100.0f;
    float move = MANUAL_CELLS_PER_S * scale * (float)dt;
    float rot = MANUAL_DEG_PER_S * scale * (float)dt;

    if (strcmp(s_robot.direction, "forward") == 0) {
        try_move(cosf(rad) * move, sinf(rad) * move);
    } else if (strcmp(s_robot.direction, "backward") == 0) {
        try_move(-cosf(rad) * move, -sinf(rad) * move);
    } else if (strcmp(s_robot.direction, "left") == 0) {
        rotate(-rot);
    } else if (strcmp(s_robot.direction, "right") == 0) {
        rotate(rot);
    }
}

void hw_robot_tick(double dt_seconds)
{
    s_robot.update_count = 0;
    update_sensors_and_map();

    if (s_robot.mode == MODE_AUTONOMOUS) {
        autonomous_step(dt_seconds);
    } else {
        manual_step(dt_seconds);
    }
}
