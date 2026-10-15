#ifndef HW_ROBOT_H
#define HW_ROBOT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define MAP_WIDTH  25
#define MAP_HEIGHT 25

#define CELL_UNKNOWN  0
#define CELL_VISITED  1
#define CELL_OBSTACLE 2

/* Nombres de canciones que devuelve hw_robot_audio_list() */
#define TRACK_NAME_MAX   64
#define TRACK_LIST_MAX   32

typedef enum {
    MODE_AUTONOMOUS = 0,
    MODE_MANUAL     = 1
} robot_mode_t;

typedef enum {
    SIM_AUDIO_STOPPED = 0,
    SIM_AUDIO_PLAYING = 1,
    SIM_AUDIO_PAUSED  = 2
} sim_audio_state_t;

typedef enum {
    NOTIF_SYSTEM_START = 0,
    NOTIF_AUTO_START   = 1,
    NOTIF_OBSTACLE     = 2,
    NOTIF_MANUAL_MODE  = 3
} notif_event_t;

typedef struct {
    int x;
    int y;
    uint8_t val;
} cell_update_t;

typedef struct {
    /* Operating Mode */
    robot_mode_t mode;

    /* Motors & Motion */
    char direction[16];   /* "forward", "backward", "left", "right", "stop" */
    uint8_t speed_percent;
    uint8_t radius_percent;
    uint8_t left_motor_speed;   /* estimado a partir del comando enviado */
    uint8_t right_motor_speed;

    /* Suction Motor */
    bool vacuum_on;

    /* Sensors
     * front_distance_cm: HC-SR04. Sin eco se reporta el alcance maximo.
     * left/right_distance_cm: los TCRT5000L no miden distancia; se reporta
     * el alcance del sensor (~1.5 cm) si hay obstaculo y -1 si esta libre.
     * Para la interfaz usar left_obstacle / right_obstacle. */
    float front_distance_cm;
    float left_distance_cm;
    float right_distance_cm;
    bool front_obstacle;
    bool left_obstacle;
    bool right_obstacle;

    /* LEDs */
    bool led_autonomous;
    bool led_manual;
    bool led_obstacle_alert;
    bool led_system_on;

    /* Audio */
    sim_audio_state_t audio_state;
    char current_track[64];
    uint8_t volume_percent;

    /* Map & Position (odometria estimada, sin encoders) */
    float robot_x;
    float robot_y;
    float heading_deg;
    uint8_t grid[MAP_HEIGHT][MAP_WIDTH];

    /* Incremental update buffer */
    cell_update_t updates[32];
    size_t update_count;
} robot_state_t;

/**
 * Inicializa librobot, los LEDs de estado y el hilo de sensores.
 * Devuelve false si el hardware no se pudo inicializar.
 */
bool hw_robot_init(void);

/**
 * Detiene el hilo de sensores, los motores y el aspirado, y libera
 * librobot. Debe llamarse una sola vez al cerrar el servidor.
 */
void hw_robot_shutdown(void);

/**
 * Avanza la maquina de estados y la odometria dt segundos
 * (llamado desde el temporizador de Mongoose).
 */
void hw_robot_tick(double dt_seconds);

/**
 * Access the global robot state.
 */
robot_state_t *hw_robot_get_state(void);

/**
 * Control API
 */
bool hw_robot_set_mode(robot_mode_t mode);
bool hw_robot_move(const char *direction, uint8_t speed, uint8_t radius);
bool hw_robot_stop(void);
bool hw_robot_set_vacuum(bool on);
bool hw_robot_set_vacuum_manual(bool on);

/**
 * Audio API
 */
bool hw_robot_audio_play(const char *track);
bool hw_robot_audio_pause(void);
bool hw_robot_audio_resume(void);
bool hw_robot_audio_stop(void);
bool hw_robot_audio_set_volume(uint8_t volume);
bool hw_robot_audio_play_notification(notif_event_t event);

/**
 * Llena names con los nombres (sin ruta) de las canciones disponibles.
 * Devuelve cuantas encontro (como maximo max_count).
 */
size_t hw_robot_audio_list(char names[][TRACK_NAME_MAX], size_t max_count);

/**
 * Map API
 */
void hw_robot_map_reset(void);

#endif /* HW_ROBOT_H */
