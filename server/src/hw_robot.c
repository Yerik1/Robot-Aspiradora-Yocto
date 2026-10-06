#include "hw_robot.h"
#include <librobot.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SENSOR_THRESHOLD_CM 18.0f
#define CELL_SIZE_CM        15.0f

static robot_state_t s_robot;

/* Autonomous state machine helper variables */
typedef enum {
    AUTO_STATE_FORWARD = 0,
    AUTO_STATE_BACKING_UP,
    AUTO_STATE_TURNING
} auto_substate_t;

static auto_substate_t s_auto_state = AUTO_STATE_FORWARD;
static double s_auto_timer = 0.0;
static float s_target_turn_angle = 0.0f;

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

void hw_robot_init(void) {
    memset(&s_robot, 0, sizeof(s_robot));
    
    robot_init();

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

    /* Initial sensors */
    s_robot.front_distance_cm = 80.0f;
    s_robot.left_distance_cm = 80.0f;
    s_robot.right_distance_cm = 80.0f;

    /* Mark initial position as visited */
    record_cell_update((int)roundf(s_robot.robot_x), (int)roundf(s_robot.robot_y), CELL_VISITED);

    printf("\033[32m[ROBOT]\033[0m Hardware robot engine initialized at position (%.1f, %.1f), mode=MANUAL\n",
           s_robot.robot_x, s_robot.robot_y);
}

robot_state_t *hw_robot_get_state(void) {
    /* Update state from hardware before returning it if needed, or rely on tick */
    bool vacuum_on;
    vacuum_is_on(&vacuum_on);
    s_robot.vacuum_on = vacuum_on;
    
    audio_state_t a_state;
    audio_get_state(&a_state);
    s_robot.audio_state = (sim_audio_state_t)a_state; // enum values match

    return &s_robot;
}

bool hw_robot_set_mode(robot_mode_t mode) {
    s_robot.mode = mode;
    if (mode == MODE_AUTONOMOUS) {
        s_robot.led_autonomous = true;
        s_robot.led_manual = false;
        
        led_set(LED_AUTONOMOUS_MODE, true);
        led_set(LED_MANUAL_MODE, false);

        hw_robot_set_vacuum(true);
        s_auto_state = AUTO_STATE_FORWARD;
        snprintf(s_robot.direction, sizeof(s_robot.direction), "forward");
        s_robot.speed_percent = 60;
        
        robot_move_forward(60);
        
        printf("\033[34m[MODE]\033[0m Switched to AUTONOMOUS mode. Suction motor turned ON.\n");
        hw_robot_audio_play_notification(NOTIF_AUTO_START);
    } else {
        s_robot.led_autonomous = false;
        s_robot.led_manual = true;
        
        led_set(LED_AUTONOMOUS_MODE, false);
        led_set(LED_MANUAL_MODE, true);
        hw_robot_set_vacuum(false);

        snprintf(s_robot.direction, sizeof(s_robot.direction), "stop");
        hw_robot_stop();
        printf("\033[34m[MODE]\033[0m Switched to MANUAL mode.\n");
        hw_robot_audio_play_notification(NOTIF_MANUAL_MODE);
    }
    return true;
}

bool hw_robot_move(const char *direction, uint8_t speed, uint8_t radius) {
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
        robot_move_forward(speed);
    } else if (strcmp(s_robot.direction, "backward") == 0) {
        robot_move_backward(speed);
    } else if (strcmp(s_robot.direction, "left") == 0) {
        robot_turn_left(speed, radius);
    } else if (strcmp(s_robot.direction, "right") == 0) {
        robot_turn_right(speed, radius);
    } else {
        robot_move_forward(0);
    }

    printf("\033[36m[TRACTION]\033[0m Direction: %s, Speed: %d%%, Radius: %d%%\n",
           s_robot.direction, speed, radius);
    return true;
}

bool hw_robot_stop(void) {
    snprintf(s_robot.direction, sizeof(s_robot.direction), "stop");
    motor_stop_all();
    return true;
}

bool hw_robot_set_vacuum(bool on) {
    s_robot.vacuum_on = on;
    if (on) {
        vacuum_on();
    } else {
        vacuum_off();
    }
    return true;
}

bool hw_robot_audio_play(const char *track) {
    snprintf(s_robot.current_track, sizeof(s_robot.current_track), "%s", track);
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "/usr/share/robot-server/server/music/%s", track);
    // Since the path might be locally on ./server/music/ during development, we'll try that too.
    if (access(full_path, F_OK) == -1) {
        snprintf(full_path, sizeof(full_path), "server/music/%s", track);
    }
    audio_play_file(full_path);
    return true;
}

bool hw_robot_audio_pause(void) {
    audio_pause();
    return true;
}

bool hw_robot_audio_resume(void) {
    audio_resume();
    return true;
}

bool hw_robot_audio_stop(void) {
    audio_stop();
    s_robot.current_track[0] = '\0';
    return true;
}

bool hw_robot_audio_set_volume(uint8_t volume) {
    s_robot.volume_percent = volume;
    audio_set_volume(volume);
    return true;
}

bool hw_robot_audio_play_notification(notif_event_t event) {
    audio_play_notification((audio_event_t)event);
    return true;
}

void hw_robot_audio_service(void) {
    // librobot.h doesn't seem to expose an audio_service. 
    // It says it ducks volume internally. So this is a no-op now.
}

void hw_robot_audio_shutdown(void) {
    robot_cleanup();
}

void hw_robot_map_reset(void) {
    memset(s_robot.grid, 0, sizeof(s_robot.grid));
    s_robot.update_count = 0;
    record_cell_update((int)roundf(s_robot.robot_x), (int)roundf(s_robot.robot_y), CELL_VISITED);
    printf("\033[32m[MAP]\033[0m Explored route map cleared.\n");
}

void hw_robot_tick(double dt_seconds) {
    /* Reset update count for this tick */
    s_robot.update_count = 0;

    /* Read real sensors */
    sensor_read_distance_cm(SENSOR_FRONT, &s_robot.front_distance_cm);
    sensor_read_distance_cm(SENSOR_LEFT, &s_robot.left_distance_cm);
    sensor_read_distance_cm(SENSOR_RIGHT, &s_robot.right_distance_cm);

    s_robot.front_obstacle = sensor_obstacle_detected(SENSOR_FRONT, SENSOR_THRESHOLD_CM);
    s_robot.left_obstacle  = sensor_obstacle_detected(SENSOR_LEFT, SENSOR_THRESHOLD_CM);
    s_robot.right_obstacle = sensor_obstacle_detected(SENSOR_RIGHT, SENSOR_THRESHOLD_CM);

    bool any_obstacle = s_robot.front_obstacle || s_robot.left_obstacle || s_robot.right_obstacle;
    if (any_obstacle != s_robot.led_obstacle_alert) {
        s_robot.led_obstacle_alert = any_obstacle;
        led_set(LED_OBSTACLE_ALERT, any_obstacle);
    }

    /* Record obstacles on the map based on current heading */
    float rad = (float)(s_robot.heading_deg * M_PI / 180.0);
    if (s_robot.front_obstacle) {
        int gx = (int)roundf(s_robot.robot_x + cosf(rad) * (s_robot.front_distance_cm / CELL_SIZE_CM));
        int gy = (int)roundf(s_robot.robot_y + sinf(rad) * (s_robot.front_distance_cm / CELL_SIZE_CM));
        record_cell_update(gx, gy, CELL_OBSTACLE);
    }
    if (s_robot.left_obstacle) {
        float rad_left = (float)((s_robot.heading_deg - 70.0f) * M_PI / 180.0);
        int gx = (int)roundf(s_robot.robot_x + cosf(rad_left) * (s_robot.left_distance_cm / CELL_SIZE_CM));
        int gy = (int)roundf(s_robot.robot_y + sinf(rad_left) * (s_robot.left_distance_cm / CELL_SIZE_CM));
        record_cell_update(gx, gy, CELL_OBSTACLE);
    }
    if (s_robot.right_obstacle) {
        float rad_right = (float)((s_robot.heading_deg + 70.0f) * M_PI / 180.0);
        int gx = (int)roundf(s_robot.robot_x + cosf(rad_right) * (s_robot.right_distance_cm / CELL_SIZE_CM));
        int gy = (int)roundf(s_robot.robot_y + sinf(rad_right) * (s_robot.right_distance_cm / CELL_SIZE_CM));
        record_cell_update(gx, gy, CELL_OBSTACLE);
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
                    hw_robot_audio_play_notification(NOTIF_OBSTACLE);
                    s_auto_state = AUTO_STATE_BACKING_UP;
                    s_auto_timer = 0.8; /* Back up for 0.8s */
                    snprintf(s_robot.direction, sizeof(s_robot.direction), "backward");
                    robot_move_backward(s_robot.speed_percent);
                } else {
                    /* Advance forward estimation */
                    float nx = s_robot.robot_x + cosf(rad) * move_speed_cells_per_sec * (float)dt_seconds;
                    float ny = s_robot.robot_y + sinf(rad) * move_speed_cells_per_sec * (float)dt_seconds;

                    int gx = (int)roundf(nx);
                    int gy = (int)roundf(ny);

                    if (gx > 0 && gx < MAP_WIDTH - 1 && gy > 0 && gy < MAP_HEIGHT - 1 && s_robot.grid[gy][gx] != CELL_OBSTACLE) {
                        s_robot.robot_x = nx;
                        s_robot.robot_y = ny;
                        record_cell_update(gx, gy, CELL_VISITED);
                    }
                }
                break;

            case AUTO_STATE_BACKING_UP:
                s_auto_timer -= dt_seconds;
                {
                    float nx = s_robot.robot_x - cosf(rad) * (move_speed_cells_per_sec * 0.8f) * (float)dt_seconds;
                    float ny = s_robot.robot_y - sinf(rad) * (move_speed_cells_per_sec * 0.8f) * (float)dt_seconds;
                    int gx = (int)roundf(nx);
                    int gy = (int)roundf(ny);
                    if (gx > 0 && gx < MAP_WIDTH - 1 && gy > 0 && gy < MAP_HEIGHT - 1 && s_robot.grid[gy][gx] != CELL_OBSTACLE) {
                        s_robot.robot_x = nx;
                        s_robot.robot_y = ny;
                        record_cell_update(gx, gy, CELL_VISITED);
                    }
                }
                if (s_auto_timer <= 0.0) {
                    s_auto_state = AUTO_STATE_TURNING;
                    s_auto_timer = 1.0; /* Turn duration */
                    /* Pick turn angle: 90 to 140 degrees */
                    s_target_turn_angle = (rand() % 2 == 0 ? 1.0f : -1.0f) * (90.0f + (rand() % 50));
                    snprintf(s_robot.direction, sizeof(s_robot.direction), s_target_turn_angle > 0 ? "right" : "left");
                    if (s_target_turn_angle > 0) {
                        robot_turn_right(s_robot.speed_percent, 0); // 0 radius = in-place turn
                    } else {
                        robot_turn_left(s_robot.speed_percent, 0);
                    }
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
                    robot_move_forward(s_robot.speed_percent);
                }
                break;
        }
    } else {
        /* Manual Mode Motion Estimation */
        float speed_scale = (float)s_robot.speed_percent / 100.0f;
        float move_speed = 2.0f * speed_scale * (float)dt_seconds;
        float rot_speed = 90.0f * speed_scale * (float)dt_seconds;

        if (strcmp(s_robot.direction, "forward") == 0) {
            float nx = s_robot.robot_x + cosf(rad) * move_speed;
            float ny = s_robot.robot_y + sinf(rad) * move_speed;
            int gx = (int)roundf(nx);
            int gy = (int)roundf(ny);

            if (gx > 0 && gx < MAP_WIDTH - 1 && gy > 0 && gy < MAP_HEIGHT - 1 && s_robot.grid[gy][gx] != CELL_OBSTACLE) {
                s_robot.robot_x = nx;
                s_robot.robot_y = ny;
                record_cell_update(gx, gy, CELL_VISITED);
            }
        } else if (strcmp(s_robot.direction, "backward") == 0) {
            float nx = s_robot.robot_x - cosf(rad) * move_speed;
            float ny = s_robot.robot_y - sinf(rad) * move_speed;
            int gx = (int)roundf(nx);
            int gy = (int)roundf(ny);

            if (gx > 0 && gx < MAP_WIDTH - 1 && gy > 0 && gy < MAP_HEIGHT - 1 && s_robot.grid[gy][gx] != CELL_OBSTACLE) {
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
