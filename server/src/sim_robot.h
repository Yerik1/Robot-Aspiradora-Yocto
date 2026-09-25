#ifndef SIM_ROBOT_H
#define SIM_ROBOT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define MAP_WIDTH  25
#define MAP_HEIGHT 25

#define CELL_UNKNOWN  0
#define CELL_VISITED  1
#define CELL_OBSTACLE 2

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
    uint8_t left_motor_speed;
    uint8_t right_motor_speed;

    /* Suction Motor */
    bool vacuum_on;

    /* Sensors */
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

    /* Map & Position */
    float robot_x;
    float robot_y;
    float heading_deg;
    uint8_t grid[MAP_HEIGHT][MAP_WIDTH];

    /* Incremental update buffer */
    cell_update_t updates[32];
    size_t update_count;
} robot_state_t;

/**
 * Initializes the simulated robot state, generates map layout with simulated obstacles.
 */
void sim_robot_init(void);

/**
 * Advances the simulation by dt seconds (called from Mongoose timer).
 */
void sim_robot_tick(double dt_seconds);

/**
 * Access the global robot state.
 */
robot_state_t *sim_robot_get_state(void);

/**
 * Control API
 */
bool sim_robot_set_mode(robot_mode_t mode);
bool sim_robot_move(const char *direction, uint8_t speed, uint8_t radius);
bool sim_robot_stop(void);
bool sim_robot_set_vacuum(bool on);

/**
 * Audio API
 */
bool sim_robot_audio_play(const char *track);
bool sim_robot_audio_pause(void);
bool sim_robot_audio_resume(void);
bool sim_robot_audio_stop(void);
bool sim_robot_audio_set_volume(uint8_t volume);
bool sim_robot_audio_play_notification(notif_event_t event);

/**
 * Map API
 */
void sim_robot_map_reset(void);

#endif /* SIM_ROBOT_H */
