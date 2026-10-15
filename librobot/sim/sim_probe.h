/**
 * sim_probe.h
 *
 * Introspeccion del hardware SIMULADO (ver pigpio_sim.c), para que
 * tests/test_librobot.c pueda hacer aserciones 100% automaticas (sin
 * confirmacion humana) cuando se compila contra el simulador.
 *
 * Esto NO existe en pigpio real ni en el build de produccion: solo lo ve
 * tests/test_librobot.c cuando sim/CMakeLists.txt define
 * LIBROBOT_SIM_PROBES al compilar test_librobot_sim.
 */
#ifndef SIM_PROBE_H
#define SIM_PROBE_H

#include "../include/librobot.h"

#ifdef __cplusplus
extern "C" {
#endif

/** % de duty (0-100) realmente aplicado en el pin fisico EN del motor,
 *  DESPUES de la compensacion optica de motor.c (apply_en_duty()).
 *  -1.0 si nunca se escribio PWM en ese pin. */
double sim_motor_pwm_percent(motor_id_t motor);

/** Nivel fisico actual de los pines de direccion IN1/IN2 de un motor.
 *  Retorna 0 si ambos se escribieron al menos una vez (dejando el valor en
 *  in1_level/in2_level), -1 si no. */
int sim_motor_dir_pins(motor_id_t motor, int *in1_level, int *in2_level);

/** Nivel fisico actual del pin de la aspiradora. -1 si nunca se escribio. */
int sim_vacuum_level(void);

/** Nivel fisico actual de un LED (0=apagado, 1=encendido). -1 si nunca se
 *  escribio. Los LEDs no llevan inversion optica (ver leds.c). */
int sim_led_level(led_id_t led);

/** Cuenta cuantas veces cambio de nivel un LED desde el ultimo reset; sirve
 *  para verificar que un led_blink() realmente esta parpadeando. */
void sim_led_reset_toggle_count(led_id_t led);
int sim_led_toggle_count(led_id_t led);

/** Cambia en caliente la distancia que "ve" un sensor simulado, para poder
 *  probar sensor_obstacle_detected()/sensor_read_distance_cm() en todas sus
 *  ramas (objeto cerca, via libre, fuera de rango/timeout) sin intervencion
 *  humana ni tener que reiniciar el proceso. */
void sim_set_distance_cm(sensor_id_t sensor, double cm);

#ifdef __cplusplus
}
#endif

#endif /* SIM_PROBE_H */
