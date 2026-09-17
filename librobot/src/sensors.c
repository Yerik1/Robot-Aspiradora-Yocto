/**
 * sensors.c
 *
 * Implementación de US-302 (sensores): lectura de distancia con los tres
 * HC-SR04 (frontal, izquierdo, derecho).
 *
 * Nota de diseño importante (trade-off de seguridad): sensor_obstacle_detected()
 * devuelve `false` (sin obstáculo) tanto cuando el sensor mide una distancia
 * mayor al umbral COMO cuando la lectura falla por timeout. Esto es correcto
 * la mayoría de las veces -- un HC-SR04 que no recibe eco normalmente
 * significa "nada dentro de ~4m", que es el caso normal en espacio abierto --
 * pero también podría enmascarar una falla real del sensor (cable suelto,
 * sensor dañado, ángulo de reflexión desfavorable) como si fuera "camino
 * libre". Para la lógica de navegación autónoma se recomienda NO confiar
 * ciegamente en esta función sola: si conviene, vigilar en la capa de
 * aplicación cuántos timeouts consecutivos da sensor_read_distance_cm() y
 * tratar una racha larga de timeouts como una condición de falla distinta
 * de "sin obstáculo" (por ejemplo, deteniendo el robot en vez de asumir vía
 * libre).
 */
#include <pigpio.h>
#include <pthread.h>
#include "../include/librobot.h"
#include "internal.h"
#include "robot_pins.h"

typedef struct {
    int trig_pin;
    int echo_pin;
} sensor_ctx_t;

static sensor_ctx_t g_sensors[3] = {
    [SENSOR_FRONT] = { PIN_SENSOR_FRONT_TRIG, PIN_SENSOR_FRONT_ECHO },
    [SENSOR_LEFT]  = { PIN_SENSOR_LEFT_TRIG,  PIN_SENSOR_LEFT_ECHO  },
    [SENSOR_RIGHT] = { PIN_SENSOR_RIGHT_TRIG, PIN_SENSOR_RIGHT_ECHO },
};

static pthread_mutex_t g_sensor_lock = PTHREAD_MUTEX_INITIALIZER;

/* Disparar dos sensores HC-SR04 casi al mismo tiempo produce cross-talk
 * entre sus ecos. g_last_trigger_tick/valid recuerdan cuándo fue el último
 * disparo (de cualquier sensor) para forzar automáticamente una separación
 * mínima de SENSOR_MIN_GAP_US antes del siguiente, sin que quien llama a
 * esta biblioteca tenga que acordarse de hacerlo. */
static uint32_t g_last_trigger_tick = 0;
static bool g_last_trigger_valid = false;

static void enforce_min_gap(void)
{
    if (!g_last_trigger_valid) return;

    uint32_t now = gpioTick();
    uint32_t elapsed = now - g_last_trigger_tick; /* resta sin signo: correcto
                                                      incluso si gpioTick()
                                                      dio la vuelta (wrap) */
    if (elapsed < SENSOR_MIN_GAP_US) {
        gpioDelay(SENSOR_MIN_GAP_US - elapsed);
    }
}

robot_status_t sensor_read_distance_cm(sensor_id_t sensor, float *distance_cm)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;
    if (sensor != SENSOR_FRONT && sensor != SENSOR_LEFT && sensor != SENSOR_RIGHT) {
        return ROBOT_ERR_INVALID_PARAM;
    }
    if (distance_cm == NULL) return ROBOT_ERR_INVALID_PARAM;

    sensor_ctx_t *s = &g_sensors[sensor];

    pthread_mutex_lock(&g_sensor_lock);

    enforce_min_gap();

    /* Pulso de disparo de 10us, tal como pide el datasheet del HC-SR04. */
    gpioTrigger(s->trig_pin, 10, 1);
    g_last_trigger_tick = gpioTick();
    g_last_trigger_valid = true;

    /* Esperar a que ECHO suba (inicio del pulso de eco). */
    uint32_t wait_start = gpioTick();
    while (gpioRead(s->echo_pin) == 0) {
        if ((gpioTick() - wait_start) > SENSOR_TIMEOUT_US) {
            pthread_mutex_unlock(&g_sensor_lock);
            return ROBOT_ERR_SENSOR_TIMEOUT;
        }
    }
    uint32_t echo_start = gpioTick();

    /* Esperar a que ECHO baje (fin del pulso de eco). */
    while (gpioRead(s->echo_pin) == 1) {
        if ((gpioTick() - echo_start) > SENSOR_TIMEOUT_US) {
            pthread_mutex_unlock(&g_sensor_lock);
            return ROBOT_ERR_SENSOR_TIMEOUT;
        }
    }
    uint32_t echo_end = gpioTick();

    pthread_mutex_unlock(&g_sensor_lock);

    uint32_t pulse_us = echo_end - echo_start;
    *distance_cm = (float)pulse_us / SENSOR_SPEED_SOUND_DIV;

    return ROBOT_OK;
}

bool sensor_obstacle_detected(sensor_id_t sensor, float threshold_cm)
{
    float distance_cm;
    robot_status_t status = sensor_read_distance_cm(sensor, &distance_cm);

    /* Ver nota de diseño al inicio del archivo: timeout se trata igual que
     * "sin obstáculo". */
    if (status != ROBOT_OK) {
        return false;
    }

    return distance_cm <= threshold_cm;
}

/* ==========================================================================
 * Init / cleanup internos (llamados por librobot.c)
 * ========================================================================== */

robot_status_t sensor_module_init(void)
{
    sensor_id_t ids[] = { SENSOR_FRONT, SENSOR_LEFT, SENSOR_RIGHT };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        sensor_ctx_t *s = &g_sensors[ids[i]];
        if (gpioSetMode(s->trig_pin, PI_OUTPUT) != 0) return ROBOT_ERR_GPIO;
        if (gpioSetMode(s->echo_pin, PI_INPUT) != 0) return ROBOT_ERR_GPIO;
        gpioWrite(s->trig_pin, 0);
    }

    g_last_trigger_valid = false;

    return ROBOT_OK;
}

void sensor_module_cleanup(void)
{
    /* No hay estado de hardware persistente que revertir más allá de dejar
     * TRIG en bajo; los pines vuelven a su default al terminar el proceso
     * (gpioTerminate() en librobot.c). Se deja explícito por claridad y
     * por si en el futuro se agrega estado (p. ej. un hilo de polling). */
    sensor_id_t ids[] = { SENSOR_FRONT, SENSOR_LEFT, SENSOR_RIGHT };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        gpioWrite(g_sensors[ids[i]].trig_pin, 0);
    }
    g_last_trigger_valid = false;
}
