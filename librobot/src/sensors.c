/**
 * sensors.c
 *
 * Sensores de proximidad del robot:
 *   - SENSOR_FRONT: ultrasónico HC-SR04, mide distancia en cm.
 *   - SENSOR_LEFT / SENSOR_RIGHT: ópticos reflectivos TCRT5000L, solo
 *     indican presencia de obstáculo (salida digital por fototransistor).
 *
 * Las funciones de este módulo bloquean mientras esperan el eco del
 * HC-SR04 (hasta SENSOR_TIMEOUT_US). El servidor debe llamarlas desde un
 * hilo dedicado a sensores, no desde el bucle de eventos.
 */
#include <pigpio.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "../include/librobot.h"
#include "internal.h"
#include "robot_pins.h"

static pthread_mutex_t g_sensor_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_last_trigger_tick = 0;
static bool g_triggered_once = false;

static bool is_ir_sensor(sensor_id_t sensor)
{
    return sensor == SENSOR_LEFT || sensor == SENSOR_RIGHT;
}

static unsigned ir_pin(sensor_id_t sensor)
{
    return (sensor == SENSOR_LEFT) ? PIN_SENSOR_LEFT_IR : PIN_SENSOR_RIGHT_IR;
}

robot_status_t sensor_module_init(void)
{
    if (gpioSetMode(PIN_SENSOR_FRONT_TRIG, PI_OUTPUT) != 0) return ROBOT_ERR_GPIO;
    if (gpioWrite(PIN_SENSOR_FRONT_TRIG, 0) != 0)           return ROBOT_ERR_GPIO;
    if (gpioSetMode(PIN_SENSOR_FRONT_ECHO, PI_INPUT) != 0)  return ROBOT_ERR_GPIO;
    if (gpioSetPullUpDown(PIN_SENSOR_FRONT_ECHO, PI_PUD_DOWN) != 0) return ROBOT_ERR_GPIO;

    /* TCRT5000L: pull-up externo de 10 kOhm, se desactiva el interno. */
    if (gpioSetMode(PIN_SENSOR_LEFT_IR, PI_INPUT) != 0)  return ROBOT_ERR_GPIO;
    if (gpioSetPullUpDown(PIN_SENSOR_LEFT_IR, PI_PUD_OFF) != 0)  return ROBOT_ERR_GPIO;
    if (gpioSetMode(PIN_SENSOR_RIGHT_IR, PI_INPUT) != 0) return ROBOT_ERR_GPIO;
    if (gpioSetPullUpDown(PIN_SENSOR_RIGHT_IR, PI_PUD_OFF) != 0) return ROBOT_ERR_GPIO;

    g_triggered_once = false;
    return ROBOT_OK;
}

void sensor_module_cleanup(void)
{
    gpioWrite(PIN_SENSOR_FRONT_TRIG, 0);
}

/* Lectura del HC-SR04 frontal. Debe llamarse con g_sensor_lock tomado. */
static robot_status_t read_front_cm(float *distance_cm)
{
    /* Respetar la separación mínima entre disparos para que se disipe el
     * eco del disparo anterior. */
    if (g_triggered_once) {
        uint32_t elapsed = gpioTick() - g_last_trigger_tick;
        if (elapsed < SENSOR_MIN_GAP_US) {
            gpioDelay(SENSOR_MIN_GAP_US - elapsed);
        }
    }

    if (gpioTrigger(PIN_SENSOR_FRONT_TRIG, 10, 1) != 0) {
        return ROBOT_ERR_GPIO;
    }
    g_last_trigger_tick = gpioTick();
    g_triggered_once = true;

    /* Esperar flanco de subida de ECHO. */
    uint32_t start = gpioTick();
    while (gpioRead(PIN_SENSOR_FRONT_ECHO) == 0) {
        if (gpioTick() - start > SENSOR_TIMEOUT_US) {
            return ROBOT_ERR_SENSOR_TIMEOUT;
        }
    }

    /* Medir el ancho del pulso de ECHO. */
    uint32_t rise = gpioTick();
    while (gpioRead(PIN_SENSOR_FRONT_ECHO) == 1) {
        if (gpioTick() - rise > SENSOR_TIMEOUT_US) {
            return ROBOT_ERR_SENSOR_TIMEOUT;
        }
    }
    uint32_t width_us = gpioTick() - rise;

    *distance_cm = (float)width_us / SENSOR_SPEED_SOUND_DIV;
    return ROBOT_OK;
}

/* Lectura de un TCRT5000L con votación por mayoría para evitar oscilaciones
 * cuando el objeto está al límite del alcance. */
static robot_status_t read_ir(sensor_id_t sensor, bool *detected)
{
    unsigned pin = ir_pin(sensor);
    int hits = 0;

    for (int i = 0; i < IR_SAMPLES; i++) {
        int level = gpioRead(pin);
        if (level < 0) {
            return ROBOT_ERR_GPIO;
        }
        if (level == IR_ACTIVE_LEVEL) {
            hits++;
        }
        if (i < IR_SAMPLES - 1) {
            gpioDelay(IR_SAMPLE_GAP_US);
        }
    }

    *detected = (hits * 2 > IR_SAMPLES);
    return ROBOT_OK;
}

robot_status_t sensor_read_distance_cm(sensor_id_t sensor, float *distance_cm)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;
    if (distance_cm == NULL)  return ROBOT_ERR_INVALID_PARAM;

    /* Los TCRT5000L no miden distancia. */
    if (sensor != SENSOR_FRONT) return ROBOT_ERR_INVALID_PARAM;

    pthread_mutex_lock(&g_sensor_lock);
    robot_status_t st = read_front_cm(distance_cm);
    pthread_mutex_unlock(&g_sensor_lock);
    return st;
}

robot_status_t sensor_read_obstacle(sensor_id_t sensor,
                                    float threshold_cm,
                                    bool *detected)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;
    if (detected == NULL)     return ROBOT_ERR_INVALID_PARAM;

    robot_status_t st;
    pthread_mutex_lock(&g_sensor_lock);

    if (is_ir_sensor(sensor)) {
        st = read_ir(sensor, detected);
    } else if (sensor == SENSOR_FRONT) {
        float cm = 0.0f;
        st = read_front_cm(&cm);
        if (st == ROBOT_ERR_SENSOR_TIMEOUT) {
            /* Sin eco: nada dentro del alcance del HC-SR04. */
            *detected = false;
            st = ROBOT_OK;
        } else if (st == ROBOT_OK) {
            *detected = (cm <= threshold_cm);
        }
    } else {
        st = ROBOT_ERR_INVALID_PARAM;
    }

    pthread_mutex_unlock(&g_sensor_lock);
    return st;
}

bool sensor_obstacle_detected(sensor_id_t sensor, float threshold_cm)
{
    bool detected = false;
    if (sensor_read_obstacle(sensor, threshold_cm, &detected) != ROBOT_OK) {
        return false;
    }
    return detected;
}
