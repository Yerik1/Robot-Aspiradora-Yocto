/**
 * internal.h
 *
 * Declaraciones privadas compartidas entre los módulos de librobot.so.
 * No se instala junto a librobot.h -- nada fuera de src/ debe incluirlo.
 */
#ifndef LIBROBOT_INTERNAL_H
#define LIBROBOT_INTERNAL_H

#include "../include/librobot.h"

/**
 * true una vez que robot_init() terminó con éxito. Todas las funciones
 * públicas de motor.c, sensors.c y leds.c la revisan antes de tocar
 * hardware y devuelven ROBOT_ERR_NOT_INITIALIZED si todavía está en false.
 */
extern bool g_robot_initialized;

/* Cada módulo expone su propio init/cleanup interno; robot_init()/
 * robot_cleanup() en librobot.c los orquesta en orden. Estas funciones
 * NO pasan por el chequeo de g_robot_initialized (son las que lo preparan
 * o lo desmontan). */

robot_status_t motor_module_init(void);
void motor_module_cleanup(void);

robot_status_t sensor_module_init(void);
void sensor_module_cleanup(void);

robot_status_t led_module_init(void);
void led_module_cleanup(void);

robot_status_t audio_module_init(void);
void audio_module_cleanup(void);

#endif /* LIBROBOT_INTERNAL_H */
