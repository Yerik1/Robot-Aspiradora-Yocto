/**
 * librobot.h
 *
 * API pública de la biblioteca dinámica librobot.so.
 * Único punto de acceso al hardware (motores, sensores, LEDs, audio)
 * permitido para el servidor web (US-402: "El servidor no accede a
 * GPIO/PWM directamente en ningún punto del código").
 *
 * Implementación cross-compilada para Raspberry Pi 4 vía toolchain Yocto.
 */

#ifndef LIBROBOT_H
#define LIBROBOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* ==========================================================================
 * Códigos de estado / error
 * ========================================================================== */

typedef enum {
    ROBOT_OK                   = 0,
    ROBOT_ERR_NOT_INITIALIZED  = -1,
    ROBOT_ERR_INVALID_PARAM    = -2,
    ROBOT_ERR_GPIO             = -3,
    ROBOT_ERR_PWM              = -4,
    ROBOT_ERR_SENSOR_TIMEOUT   = -5,
    ROBOT_ERR_AUDIO            = -6,
    ROBOT_ERR_AUDIO_FILE_NOT_FOUND = -7
} robot_status_t;

/* ==========================================================================
 * Inicialización / limpieza
 * ========================================================================== */

/**
 * Inicializa GPIO, PWM y el subsistema de audio. Debe llamarse una sola vez
 * antes de usar cualquier otra función de la biblioteca.
 */
robot_status_t robot_init(void);

/**
 * Libera GPIO/PWM, detiene motores y audio, y cierra recursos abiertos.
 * Debe llamarse al finalizar el programa (incluyendo manejo de señales
 * como SIGTERM en el servicio systemd).
 */
robot_status_t robot_cleanup(void);

/* ==========================================================================
 * Control de tracción (motores DC, PWM)
 * ========================================================================== */

typedef enum {
    MOTOR_LEFT  = 0,
    MOTOR_RIGHT = 1
} motor_id_t;

typedef enum {
    MOTOR_DIR_FORWARD  = 0,
    MOTOR_DIR_BACKWARD = 1
} motor_direction_t;

/**
 * Controla un motor individual.
 * @param duty_cycle_percent  velocidad, 0-100.
 */
robot_status_t motor_set_speed(motor_id_t motor,
                                uint8_t duty_cycle_percent,
                                motor_direction_t direction);

/** Detiene un motor específico (duty cycle a 0). */
robot_status_t motor_stop(motor_id_t motor);

/** Detiene ambos motores. Se debe poder invocar en cualquier estado. */
robot_status_t motor_stop_all(void);

/* Movimientos compuestos de alto nivel, para uso directo del servidor web
 * y del algoritmo de navegación autónoma. Internamente llaman a
 * motor_set_speed() sobre ambos motores. */

robot_status_t robot_move_forward(uint8_t speed_percent);
robot_status_t robot_move_backward(uint8_t speed_percent);

/**
 * Gira usando velocidades diferenciales entre ambos motores.
 * @param radius_percent  0 = giro sobre el propio eje, 100 = giro lo más
 *                        amplio posible (motor exterior a speed_percent,
 *                        interior reducido proporcionalmente).
 */
robot_status_t robot_turn_left(uint8_t speed_percent, uint8_t radius_percent);
robot_status_t robot_turn_right(uint8_t speed_percent, uint8_t radius_percent);

/* ==========================================================================
 * Motor de aspirado (succión)
 * ========================================================================== */

/**
 * Enciende el motor de aspirado. A diferencia de los motores de tracción,
 * este motor gira en un solo sentido: no tiene parámetro de dirección ni
 * de velocidad, solo encendido/apagado (US-301/302 lo tratan como un
 * actuador independiente de MOTOR_LEFT/MOTOR_RIGHT).
 */
robot_status_t vacuum_on(void);

/** Apaga el motor de aspirado. Debe poder invocarse en cualquier estado. */
robot_status_t vacuum_off(void);

/** Consulta si el motor de aspirado está encendido actualmente. */
robot_status_t vacuum_is_on(bool *is_on);

/* ==========================================================================
 * Sensores de proximidad
 * ========================================================================== */

typedef enum {
    SENSOR_FRONT = 0,   /* 12:00 */
    SENSOR_LEFT  = 1,   /* 9:00  - lateral izquierdo */
    SENSOR_RIGHT = 2    /* 3:00  - lateral derecho   */
} sensor_id_t;

/** Lee la distancia del sensor indicado. */
robot_status_t sensor_read_distance_cm(sensor_id_t sensor, float *distance_cm);

/** true si la distancia leída es menor o igual a threshold_cm. */
bool sensor_obstacle_detected(sensor_id_t sensor, float threshold_cm);

/* ==========================================================================
 * LEDs de estado
 * ========================================================================== */

typedef enum {
    LED_AUTONOMOUS_MODE = 0,   /* modo autónomo activo   */
    LED_MANUAL_MODE     = 1,   /* modo manual activo     */
    LED_OBSTACLE_ALERT  = 2,   /* obstáculo detectado    */
    LED_SYSTEM_ON       = 3    /* sistema encendido      */
} led_id_t;

robot_status_t led_set(led_id_t led, bool on);
robot_status_t led_toggle(led_id_t led);

/**
 * Hace parpadear un LED en un hilo/proceso no bloqueante.
 * @param freq_hz      frecuencia del parpadeo.
 * @param duration_ms  0 = parpadea indefinidamente hasta led_set(led, false)
 *                     o led_stop_blink(led).
 */
robot_status_t led_blink(led_id_t led, uint32_t freq_hz, uint32_t duration_ms);
robot_status_t led_stop_blink(led_id_t led);

/* ==========================================================================
 * Audio (reproducción MP3 vía mpg123, proceso no bloqueante)
 * ========================================================================== */

typedef enum {
    AUDIO_STOPPED = 0,
    AUDIO_PLAYING = 1,
    AUDIO_PAUSED  = 2
} audio_state_t;

/** Eventos de notificación sonora obligatorios (Task 3.3 / especificación). */
typedef enum {
    AUDIO_EVENT_SYSTEM_START     = 0,
    AUDIO_EVENT_AUTONOMOUS_START = 1,
    AUDIO_EVENT_OBSTACLE         = 2,
    AUDIO_EVENT_MANUAL_MODE      = 3
} audio_event_t;

/** Reproduce un archivo MP3 específico (ruta absoluta o relativa al rootfs). */
robot_status_t audio_play_file(const char *filepath);

/** Reproduce el sonido corto de notificación asociado al evento. */
robot_status_t audio_play_notification(audio_event_t event);

robot_status_t audio_pause(void);
robot_status_t audio_resume(void);
robot_status_t audio_stop(void);

/** @param volume_percent  0-100. */
robot_status_t audio_set_volume(uint8_t volume_percent);
robot_status_t audio_get_volume(uint8_t *volume_percent);

robot_status_t audio_get_state(audio_state_t *state);

/**
 * Lista los archivos MP3 disponibles en el directorio de audio del rootfs.
 * @param out_paths   arreglo provisto por el llamador, tamaño max_count.
 * @param max_count   capacidad de out_paths.
 * @param out_found   cantidad real de archivos encontrados.
 */
robot_status_t audio_list_files(char out_paths[][256],
                                 uint32_t max_count,
                                 uint32_t *out_found);

#ifdef __cplusplus
}
#endif

#endif /* LIBROBOT_H */
