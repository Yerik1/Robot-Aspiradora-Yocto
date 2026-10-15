/**
 * pigpio_sim.c
 *
 * Implementacion simulada de la libpigpio, para poder correr el codigo de
 * produccion (src/motor.c, src/sensors.c, src/leds.c, src/librobot.c, SIN
 * modificarlos) en un host de desarrollo sin Raspberry Pi ni hardware real.
 *
 * Que simula y para que sirve:
 *   - gpioHardwarePWM(): registra y loguea cada valor de duty/frecuencia
 *     que motor.c calcula y "envia" al pin EN de cada motor. Esto permite
 *     verificar la MATEMATICA de motor.c (rampa, compensacion optica) sin
 *     necesitar un osciloscopio sobre el L298N real.
 *   - gpioWrite()/gpioRead(): mantienen el nivel logico de cada GPIO en
 *     memoria, y loguean cada escritura (direccion de motores, LEDs,
 *     aspiradora).
 *   - gpioTrigger()/gpioRead() sobre los pines ECHO: simulan el eco de un
 *     HC-SR04 devolviendo un pulso de duracion proporcional a una
 *     distancia configurable (SIM_FRONT_CM / SIM_LEFT_CM / SIM_RIGHT_CM),
 *     para poder ejercitar sensors.c (temporizacion trigger/echo, timeout,
 *     separacion anti cross-talk) de punta a punta.
 *   - gpioTick()/gpioDelay(): usan reloj monotonico real y usleep() real,
 *     asi que las rampas, los parpadeos de LED y los timeouts de sensor se
 *     comportan con la misma temporizacion relativa que en la Pi real.
 *
 * Lo que esto NO prueba: la senial electrica real en el pin fisico, ni que
 * el motor/sensor/LED fisico responda como se espera. Eso requiere la
 * Raspberry Pi con el hardware conectado (ver tests/test_librobot.c).
 *
 * Mapeo TRIG->ECHO usado para simular el eco: copiado de
 * src/robot_pins.h (FRONT: trig16/echo4, LEFT: trig6/echo5,
 * RIGHT: trig24/echo25). Si robot_pins.h cambia, actualizar aqui tambien.
 */
#define _POSIX_C_SOURCE 200809L

#include "pigpio.h"
#include "sim_probe.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define NUM_PINS 32

typedef struct {
    int mode;
    int level;
    int level_set;          /* se escribio al menos una vez */
    int toggle_count;       /* cambios de nivel desde el ultimo reset */
    int last_toggle_level;
    int has_pwm;
    unsigned pwm_duty_raw;  /* 0-1000000, tal como lo recibe gpioHardwarePWM */
    int armed;
    uint32_t trig_tick;
} pin_state_t;

static pin_state_t g_pins[NUM_PINS];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct timespec g_t0;

static double g_front_cm = 40.0;
static double g_left_cm = 40.0;
static double g_right_cm = 40.0;

/* Mapeo motor/LED -> GPIO, copiado de src/robot_pins.h (indices 0/1 de
 * motor_id_t = LEFT/RIGHT; indices 0-3 de led_id_t = AUTONOMOUS, MANUAL,
 * OBSTACLE, SYSTEM). Duplicado a proposito: robot_pins.h es privado a
 * src/ y este simulador no debe depender de el. */
static const unsigned MOTOR_EN_PIN[2]  = { 18, 19 };
static const unsigned MOTOR_IN1_PIN[2] = { 17, 22 };
static const unsigned MOTOR_IN2_PIN[2] = { 27, 23 };
#define VACUUM_PIN 26u
static const unsigned LED_GPIO[4] = { 12, 13, 20, 21 };

typedef struct {
    int trig;
    int echo;
    double *sim_cm;
    const char *name;
} echo_map_t;

static echo_map_t g_map[] = {
    { 16, 4,  &g_front_cm, "SENSOR_FRONT" },
    { 6,  5,  &g_left_cm,  "SENSOR_LEFT"  },
    { 24, 25, &g_right_cm, "SENSOR_RIGHT" },
};
#define MAP_N (sizeof(g_map) / sizeof(g_map[0]))

static const char *pin_label(unsigned gpio)
{
    switch (gpio) {
        case 17: return "MOTOR_LEFT_IN1";
        case 27: return "MOTOR_LEFT_IN2";
        case 18: return "MOTOR_LEFT_EN(PWM)";
        case 22: return "MOTOR_RIGHT_IN1";
        case 23: return "MOTOR_RIGHT_IN2";
        case 19: return "MOTOR_RIGHT_EN(PWM)";
        case 26: return "VACUUM_ENABLE";
        case 16: return "SENSOR_FRONT_TRIG";
        case 4:  return "SENSOR_FRONT_ECHO";
        case 6:  return "SENSOR_LEFT_TRIG";
        case 5:  return "SENSOR_LEFT_ECHO";
        case 24: return "SENSOR_RIGHT_TRIG";
        case 25: return "SENSOR_RIGHT_ECHO";
        case 12: return "LED_AUTONOMOUS_MODE";
        case 13: return "LED_MANUAL_MODE";
        case 20: return "LED_OBSTACLE_ALERT";
        case 21: return "LED_SYSTEM_ON";
        default: return "?";
    }
}

static uint32_t now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    uint64_t us = (uint64_t)(t.tv_sec - g_t0.tv_sec) * 1000000ULL +
                  (uint64_t)(t.tv_nsec - g_t0.tv_nsec) / 1000ULL;
    return (uint32_t)us;
}

int gpioInitialise(void)
{
    /* Los fprintf(stderr, ...) de este archivo son solo para dejar un log
     * legible; por defecto stderr no tiene buffer, y cada llamada hace un
     * syscall write() inmediato. En hosts donde ese write es lento (p. ej.
     * corriendo desde un punto de montaje 9p/DrvFs de WSL sobre /mnt/c), esa
     * latencia se cuela DENTRO de la ventana de tiempo que sensors.c mide
     * para el eco de un HC-SR04 (unos cientos de microsegundos para objetos
     * cercanos), y arruina la simulacion del timing sin que haya ningun
     * problema real en sensors.c. Bufferizar stderr evita que el logueo
     * interfiera con la temporizacion simulada. */
    static char stderr_buf[1 << 16];
    setvbuf(stderr, stderr_buf, _IOFBF, sizeof(stderr_buf));

    clock_gettime(CLOCK_MONOTONIC, &g_t0);
    memset(g_pins, 0, sizeof(g_pins));

    const char *front = getenv("SIM_FRONT_CM");
    const char *left = getenv("SIM_LEFT_CM");
    const char *right = getenv("SIM_RIGHT_CM");
    if (front) g_front_cm = atof(front);
    if (left) g_left_cm = atof(left);
    if (right) g_right_cm = atof(right);

    fprintf(stderr, "[SIM] gpioInitialise() -- distancias simuladas: FRONT=%.1fcm LEFT=%.1fcm RIGHT=%.1fcm\n"
                     "[SIM] (ajustables con las variables de entorno SIM_FRONT_CM / SIM_LEFT_CM / SIM_RIGHT_CM)\n",
            g_front_cm, g_left_cm, g_right_cm);
    return 0;
}

void gpioTerminate(void)
{
    fprintf(stderr, "[SIM %8u us] gpioTerminate()\n", now_us());
    fflush(stderr);
}

int gpioSetMode(unsigned gpio, unsigned mode)
{
    if (gpio >= NUM_PINS) return -1;
    g_pins[gpio].mode = (int)mode;
    fprintf(stderr, "[SIM %8u us] gpioSetMode(gpio=%2u [%s], mode=%s)\n",
            now_us(), gpio, pin_label(gpio), mode == PI_OUTPUT ? "OUTPUT" : "INPUT");
    return 0;
}

int gpioWrite(unsigned gpio, unsigned level)
{
    if (gpio >= NUM_PINS) return -1;
    int newlevel = level ? 1 : 0;

    pthread_mutex_lock(&g_lock);
    if (g_pins[gpio].level_set && newlevel != g_pins[gpio].last_toggle_level) {
        g_pins[gpio].toggle_count++;
    }
    g_pins[gpio].last_toggle_level = newlevel;
    g_pins[gpio].level = newlevel;
    g_pins[gpio].level_set = 1;
    pthread_mutex_unlock(&g_lock);

    fprintf(stderr, "[SIM %8u us] gpioWrite(gpio=%2u [%s], level=%u)\n",
            now_us(), gpio, pin_label(gpio), level);
    return 0;
}

int gpioRead(unsigned gpio)
{
    if (gpio >= NUM_PINS) return 0;

    for (size_t i = 0; i < MAP_N; i++) {
        if ((unsigned)g_map[i].echo == gpio) {
            pthread_mutex_lock(&g_lock);
            int armed = g_pins[gpio].armed;
            uint32_t trig_tick = g_pins[gpio].trig_tick;
            pthread_mutex_unlock(&g_lock);

            if (!armed) return 0;

            uint32_t elapsed = now_us() - trig_tick;
            const uint32_t pre_delay_us = 300; /* tiempo simulado de ida del pulso de sonido */
            uint32_t pulse_us = (uint32_t)(*(g_map[i].sim_cm) * 58.0);

            if (elapsed < pre_delay_us) return 0;
            if (elapsed < pre_delay_us + pulse_us) return 1;
            return 0;
        }
    }
    return g_pins[gpio].level;
}

int gpioTrigger(unsigned gpio, unsigned pulseLen, unsigned level)
{
    (void)pulseLen;
    (void)level;
    for (size_t i = 0; i < MAP_N; i++) {
        if ((unsigned)g_map[i].trig == gpio) {
            pthread_mutex_lock(&g_lock);
            g_pins[g_map[i].echo].armed = 1;
            g_pins[g_map[i].echo].trig_tick = now_us();
            pthread_mutex_unlock(&g_lock);
            fprintf(stderr, "[SIM %8u us] gpioTrigger(%s, trig_gpio=%u) -- eco simulado en gpio=%d (~%.1fcm)\n",
                    now_us(), g_map[i].name, gpio, g_map[i].echo, *(g_map[i].sim_cm));
            return 0;
        }
    }
    fprintf(stderr, "[SIM %8u us] gpioTrigger(gpio=%u) -- sin mapeo de eco conocido\n", now_us(), gpio);
    return 0;
}

int gpioHardwarePWM(unsigned gpio, unsigned pwmFreq, unsigned pwmDuty)
{
    if (gpio < NUM_PINS) {
        pthread_mutex_lock(&g_lock);
        g_pins[gpio].has_pwm = 1;
        g_pins[gpio].pwm_duty_raw = pwmDuty;
        pthread_mutex_unlock(&g_lock);
    }

    double duty_pct_en_el_pin = (double)pwmDuty / 1000000.0 * 100.0;
    fprintf(stderr, "[SIM %8u us] gpioHardwarePWM(gpio=%2u [%s], freq=%uHz, raw=%u/1000000 => %.1f%% en el pin fisico)\n",
            now_us(), gpio, pin_label(gpio), pwmFreq, pwmDuty, duty_pct_en_el_pin);
    return 0;
}

uint32_t gpioTick(void)
{
    return now_us();
}

uint32_t gpioDelay(uint32_t micros)
{
    struct timespec req;
    req.tv_sec = micros / 1000000U;
    req.tv_nsec = (long)(micros % 1000000U) * 1000L;
    nanosleep(&req, NULL);
    return micros;
}

/* ==========================================================================
 * Introspeccion para pruebas automaticas (ver sim_probe.h)
 * ========================================================================== */

double sim_motor_pwm_percent(motor_id_t motor)
{
    if (motor != MOTOR_LEFT && motor != MOTOR_RIGHT) return -1.0;
    unsigned gpio = MOTOR_EN_PIN[motor];
    pthread_mutex_lock(&g_lock);
    int has = g_pins[gpio].has_pwm;
    unsigned raw = g_pins[gpio].pwm_duty_raw;
    pthread_mutex_unlock(&g_lock);
    if (!has) return -1.0;
    return (double)raw / 1000000.0 * 100.0;
}

int sim_motor_dir_pins(motor_id_t motor, int *in1_level, int *in2_level)
{
    if (motor != MOTOR_LEFT && motor != MOTOR_RIGHT) return -1;
    unsigned in1 = MOTOR_IN1_PIN[motor];
    unsigned in2 = MOTOR_IN2_PIN[motor];
    pthread_mutex_lock(&g_lock);
    int set = g_pins[in1].level_set && g_pins[in2].level_set;
    int l1 = g_pins[in1].level;
    int l2 = g_pins[in2].level;
    pthread_mutex_unlock(&g_lock);
    if (!set) return -1;
    if (in1_level) *in1_level = l1;
    if (in2_level) *in2_level = l2;
    return 0;
}

int sim_vacuum_level(void)
{
    pthread_mutex_lock(&g_lock);
    int set = g_pins[VACUUM_PIN].level_set;
    int lvl = g_pins[VACUUM_PIN].level;
    pthread_mutex_unlock(&g_lock);
    return set ? lvl : -1;
}

int sim_led_level(led_id_t led)
{
    if (led < LED_AUTONOMOUS_MODE || led > LED_SYSTEM_ON) return -1;
    unsigned gpio = LED_GPIO[led];
    pthread_mutex_lock(&g_lock);
    int set = g_pins[gpio].level_set;
    int lvl = g_pins[gpio].level;
    pthread_mutex_unlock(&g_lock);
    return set ? lvl : -1;
}

void sim_led_reset_toggle_count(led_id_t led)
{
    if (led < LED_AUTONOMOUS_MODE || led > LED_SYSTEM_ON) return;
    unsigned gpio = LED_GPIO[led];
    pthread_mutex_lock(&g_lock);
    g_pins[gpio].toggle_count = 0;
    pthread_mutex_unlock(&g_lock);
}

int sim_led_toggle_count(led_id_t led)
{
    if (led < LED_AUTONOMOUS_MODE || led > LED_SYSTEM_ON) return 0;
    unsigned gpio = LED_GPIO[led];
    pthread_mutex_lock(&g_lock);
    int c = g_pins[gpio].toggle_count;
    pthread_mutex_unlock(&g_lock);
    return c;
}

void sim_set_distance_cm(sensor_id_t sensor, double cm)
{
    pthread_mutex_lock(&g_lock);
    switch (sensor) {
        case SENSOR_FRONT: g_front_cm = cm; break;
        case SENSOR_LEFT:  g_left_cm = cm; break;
        case SENSOR_RIGHT: g_right_cm = cm; break;
        default: break;
    }
    pthread_mutex_unlock(&g_lock);
}
