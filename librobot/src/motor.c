/**
 * motor.c
 *
 * Implementación de US-301 (tracción): control de los dos motores DC vía
 * L298N, y del motor de aspirado (encendido/apagado simple).
 *
 * Nota sobre polaridad: los pines IN1/IN2/IN3/IN4 y los pines EN (PWM) del
 * L298N NO se controlan directamente -- pasan por optoacopladores PC817
 * para mantener aislamiento galvánico entre la tierra lógica de la
 * Raspberry Pi ("rasp gnd") y la tierra de potencia de motores/batería
 * ("motor gnd"). Cada optoacoplador invierte la señal:
 *   GPIO en alto  -> LED del optoacoplador enciende -> fototransistor
 *                    conduce -> el colector se va a "motor gnd" (bajo).
 *   GPIO en bajo  -> LED apagado -> el pull-up hacia "motor vcc" deja el
 *                    colector en alto.
 * Es decir, el nivel lógico que el L298N recibe es el INVERSO del nivel
 * que pigpio escribe en el GPIO. set_logical_pin() de abajo hace esa
 * traducción para que el resto del archivo piense en términos del nivel
 * "deseado en el L298N", no del nivel físico del GPIO.
 *
 * Lo mismo aplica al duty cycle de PWM sobre los pines EN: como también
 * pasan por un optoacoplador, el duty efectivo que ve el L298N es
 * (100 - duty_programado). apply_en_duty() compensa esto.
 */
#include <pigpio.h>
#include <pthread.h>
#include <string.h>
#include "../include/librobot.h"
#include "internal.h"
#include "robot_pins.h"

/* ==========================================================================
 * Estado interno
 * ========================================================================== */

typedef struct {
    int in1_pin;
    int in2_pin;
    int en_pin;
    uint8_t current_duty;          /* 0-100, último duty aplicado */
    motor_direction_t current_dir;
} motor_ctx_t;

static motor_ctx_t g_motors[2] = {
    [MOTOR_LEFT]  = { PIN_MOTOR_LEFT_IN1,  PIN_MOTOR_LEFT_IN2,  PIN_MOTOR_LEFT_EN,  0, MOTOR_DIR_FORWARD },
    [MOTOR_RIGHT] = { PIN_MOTOR_RIGHT_IN1, PIN_MOTOR_RIGHT_IN2, PIN_MOTOR_RIGHT_EN, 0, MOTOR_DIR_FORWARD },
};

static pthread_mutex_t g_motor_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_vacuum_on = false;

/* Parámetros de la rampa bloqueante usada al cambiar de velocidad/sentido.
 * Ver nota de diseño en la respuesta: es una simplificación deliberada;
 * si en el futuro interfiere con la reacción a obstáculos, se puede mover
 * a un hilo de fondo igual que se hizo con el parpadeo de LEDs. */
#define RAMP_STEP_PERCENT   5
#define RAMP_STEP_DELAY_US  15000

/* ==========================================================================
 * Helpers de bajo nivel (aplican la inversión óptica)
 * ========================================================================== */

/** Escribe en un pin de dirección (IN1..IN4) el nivel LÓGICO deseado en el
 *  lado del L298N, compensando la inversión del optoacoplador. */
static void set_logical_pin(int gpio, bool logical_high)
{
    gpioWrite(gpio, logical_high ? 0 : 1);
}

/** Aplica al pin EN el duty cycle deseado en el lado del L298N (0-100),
 *  compensando la inversión del optoacoplador sobre el PWM. */
static void apply_en_duty(int gpio, uint8_t duty_percent)
{
    double compensated = 100.0 - (double)duty_percent;
    unsigned pwm_value = (unsigned)((compensated / 100.0) * 1000000.0);
    gpioHardwarePWM(gpio, MOTOR_PWM_FREQ_HZ, pwm_value);
}

/** Fija los pines de dirección de un motor según motor_direction_t. */
static void set_direction_pins(motor_ctx_t *m, motor_direction_t dir)
{
    if (dir == MOTOR_DIR_FORWARD) {
        set_logical_pin(m->in1_pin, true);
        set_logical_pin(m->in2_pin, false);
    } else {
        set_logical_pin(m->in1_pin, false);
        set_logical_pin(m->in2_pin, true);
    }
}

/** Lleva el duty de un motor desde su valor actual hasta target_duty en
 *  pasos de RAMP_STEP_PERCENT, bloqueando el hilo llamador. No cambia
 *  dirección -- eso lo maneja ramp_to_speed(). */
static void ramp_duty(motor_ctx_t *m, uint8_t target_duty)
{
    int current = m->current_duty;
    int target = target_duty;
    int step = (target >= current) ? RAMP_STEP_PERCENT : -RAMP_STEP_PERCENT;

    while (current != target) {
        current += step;
        if ((step > 0 && current > target) || (step < 0 && current < target)) {
            current = target;
        }
        apply_en_duty(m->en_pin, (uint8_t)current);
        m->current_duty = (uint8_t)current;
        if (current != target) {
            gpioDelay(RAMP_STEP_DELAY_US);
        }
    }
}

/**
 * Cambia velocidad/dirección de un motor de forma segura: si la dirección
 * solicitada difiere de la actual y el motor tiene duty distinto de 0,
 * primero rampa a 0, cambia los pines de dirección, y luego rampa hacia el
 * nuevo target. Esto evita una inversión abrupta tipo "plugging" que
 * estresa el motor y el puente H.
 */
static void ramp_to_speed(motor_ctx_t *m, uint8_t target_duty, motor_direction_t dir)
{
    if (dir != m->current_dir && m->current_duty != 0) {
        ramp_duty(m, 0);
    }
    if (dir != m->current_dir) {
        set_direction_pins(m, dir);
        m->current_dir = dir;
    }
    ramp_duty(m, target_duty);
}

/* ==========================================================================
 * API pública -- US-301
 * ========================================================================== */

robot_status_t motor_set_speed(motor_id_t motor,
                                uint8_t duty_cycle_percent,
                                motor_direction_t direction)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;
    if (motor != MOTOR_LEFT && motor != MOTOR_RIGHT) return ROBOT_ERR_INVALID_PARAM;
    if (duty_cycle_percent > 100) return ROBOT_ERR_INVALID_PARAM;
    if (direction != MOTOR_DIR_FORWARD && direction != MOTOR_DIR_BACKWARD) {
        return ROBOT_ERR_INVALID_PARAM;
    }

    pthread_mutex_lock(&g_motor_lock);
    ramp_to_speed(&g_motors[motor], duty_cycle_percent, direction);
    pthread_mutex_unlock(&g_motor_lock);

    return ROBOT_OK;
}

robot_status_t motor_stop(motor_id_t motor)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;
    if (motor != MOTOR_LEFT && motor != MOTOR_RIGHT) return ROBOT_ERR_INVALID_PARAM;

    pthread_mutex_lock(&g_motor_lock);
    ramp_duty(&g_motors[motor], 0);
    pthread_mutex_unlock(&g_motor_lock);

    return ROBOT_OK;
}

robot_status_t motor_stop_all(void)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;

    pthread_mutex_lock(&g_motor_lock);
    ramp_duty(&g_motors[MOTOR_LEFT], 0);
    ramp_duty(&g_motors[MOTOR_RIGHT], 0);
    pthread_mutex_unlock(&g_motor_lock);

    return ROBOT_OK;
}

robot_status_t robot_move_forward(uint8_t speed_percent)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;
    if (speed_percent > 100) return ROBOT_ERR_INVALID_PARAM;

    pthread_mutex_lock(&g_motor_lock);
    ramp_to_speed(&g_motors[MOTOR_LEFT], speed_percent, MOTOR_DIR_FORWARD);
    ramp_to_speed(&g_motors[MOTOR_RIGHT], speed_percent, MOTOR_DIR_FORWARD);
    pthread_mutex_unlock(&g_motor_lock);

    return ROBOT_OK;
}

robot_status_t robot_move_backward(uint8_t speed_percent)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;
    if (speed_percent > 100) return ROBOT_ERR_INVALID_PARAM;

    pthread_mutex_lock(&g_motor_lock);
    ramp_to_speed(&g_motors[MOTOR_LEFT], speed_percent, MOTOR_DIR_BACKWARD);
    ramp_to_speed(&g_motors[MOTOR_RIGHT], speed_percent, MOTOR_DIR_BACKWARD);
    pthread_mutex_unlock(&g_motor_lock);

    return ROBOT_OK;
}

/**
 * Calcula velocidad/dirección de la rueda interior de un giro.
 * radius_percent=0   -> gira sobre el propio eje (rueda interior a
 *                       velocidad completa en reversa).
 * radius_percent=100 -> línea recta (rueda interior = rueda exterior).
 * Interpolación lineal entre esos dos extremos:
 *   inner_signed = speed_percent * (2*radius_percent/100 - 1)
 * El signo de inner_signed determina la dirección; su valor absoluto es
 * la magnitud de la velocidad a aplicar.
 */
static void apply_turn(motor_id_t outer, motor_id_t inner,
                        uint8_t speed_percent, uint8_t radius_percent)
{
    double factor = (2.0 * (double)radius_percent / 100.0) - 1.0;
    double inner_signed = (double)speed_percent * factor;

    motor_direction_t inner_dir = (inner_signed >= 0.0) ? MOTOR_DIR_FORWARD : MOTOR_DIR_BACKWARD;
    uint8_t inner_speed = (uint8_t)(inner_signed < 0.0 ? -inner_signed : inner_signed);

    pthread_mutex_lock(&g_motor_lock);
    ramp_to_speed(&g_motors[outer], speed_percent, MOTOR_DIR_FORWARD);
    ramp_to_speed(&g_motors[inner], inner_speed, inner_dir);
    pthread_mutex_unlock(&g_motor_lock);
}

robot_status_t robot_turn_left(uint8_t speed_percent, uint8_t radius_percent)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;
    if (speed_percent > 100 || radius_percent > 100) return ROBOT_ERR_INVALID_PARAM;

    /* Girar a la izquierda: la rueda izquierda es la interior. */
    apply_turn(MOTOR_RIGHT, MOTOR_LEFT, speed_percent, radius_percent);
    return ROBOT_OK;
}

robot_status_t robot_turn_right(uint8_t speed_percent, uint8_t radius_percent)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;
    if (speed_percent > 100 || radius_percent > 100) return ROBOT_ERR_INVALID_PARAM;

    /* Girar a la derecha: la rueda derecha es la interior. */
    apply_turn(MOTOR_LEFT, MOTOR_RIGHT, speed_percent, radius_percent);
    return ROBOT_OK;
}

/* ==========================================================================
 * Motor de aspirado -- encendido/apagado simple
 *
 * Ver comentario en robot_pins.h sobre PIN_VACUUM_ENABLE: la etapa de
 * switcheo (MOSFET + optoacoplador) todavía no está dibujada en el
 * esquemático. Se asume aquí, como placeholder razonable, la misma
 * convención de inversión óptica que el resto del circuito (GPIO alto ->
 * LED del optoacoplador enciende -> "activa" la etapa de switcheo).
 * AJUSTAR set_logical_pin()/nivel si el circuito real resulta distinto.
 * ========================================================================== */

robot_status_t vacuum_on(void)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;

    pthread_mutex_lock(&g_motor_lock);
    set_logical_pin(PIN_VACUUM_ENABLE, true);
    g_vacuum_on = true;
    pthread_mutex_unlock(&g_motor_lock);

    return ROBOT_OK;
}

robot_status_t vacuum_off(void)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;

    pthread_mutex_lock(&g_motor_lock);
    set_logical_pin(PIN_VACUUM_ENABLE, false);
    g_vacuum_on = false;
    pthread_mutex_unlock(&g_motor_lock);

    return ROBOT_OK;
}

robot_status_t vacuum_is_on(bool *is_on)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;
    if (is_on == NULL) return ROBOT_ERR_INVALID_PARAM;

    pthread_mutex_lock(&g_motor_lock);
    *is_on = g_vacuum_on;
    pthread_mutex_unlock(&g_motor_lock);

    return ROBOT_OK;
}

/* ==========================================================================
 * Init / cleanup internos (llamados por librobot.c)
 * ========================================================================== */

robot_status_t motor_module_init(void)
{
    int pins[] = {
        PIN_MOTOR_LEFT_IN1, PIN_MOTOR_LEFT_IN2, PIN_MOTOR_LEFT_EN,
        PIN_MOTOR_RIGHT_IN1, PIN_MOTOR_RIGHT_IN2, PIN_MOTOR_RIGHT_EN,
        PIN_VACUUM_ENABLE,
    };
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        if (gpioSetMode(pins[i], PI_OUTPUT) != 0) {
            return ROBOT_ERR_GPIO;
        }
    }

    g_motors[MOTOR_LEFT].current_duty = 0;
    g_motors[MOTOR_LEFT].current_dir = MOTOR_DIR_FORWARD;
    g_motors[MOTOR_RIGHT].current_duty = 0;
    g_motors[MOTOR_RIGHT].current_dir = MOTOR_DIR_FORWARD;
    g_vacuum_on = false;

    /* Estado de reposo seguro: ambos motores parados, dirección FORWARD
     * por convención, aspiradora apagada. */
    set_direction_pins(&g_motors[MOTOR_LEFT], MOTOR_DIR_FORWARD);
    set_direction_pins(&g_motors[MOTOR_RIGHT], MOTOR_DIR_FORWARD);
    apply_en_duty(g_motors[MOTOR_LEFT].en_pin, 0);
    apply_en_duty(g_motors[MOTOR_RIGHT].en_pin, 0);
    set_logical_pin(PIN_VACUUM_ENABLE, false);

    return ROBOT_OK;
}

/**
 * IMPORTANTE: esta función se llama durante robot_cleanup(), DESPUÉS de
 * que g_robot_initialized ya se puso en false. Por eso manipula pines y
 * estado directamente en vez de llamar a motor_stop_all()/vacuum_off(),
 * que están bloqueadas por ese chequeo y harían un no-op silencioso,
 * dejando motores/aspiradora encendidos.
 */
void motor_module_cleanup(void)
{
    apply_en_duty(g_motors[MOTOR_LEFT].en_pin, 0);
    apply_en_duty(g_motors[MOTOR_RIGHT].en_pin, 0);
    set_logical_pin(g_motors[MOTOR_LEFT].in1_pin, false);
    set_logical_pin(g_motors[MOTOR_LEFT].in2_pin, false);
    set_logical_pin(g_motors[MOTOR_RIGHT].in1_pin, false);
    set_logical_pin(g_motors[MOTOR_RIGHT].in2_pin, false);
    set_logical_pin(PIN_VACUUM_ENABLE, false);

    g_motors[MOTOR_LEFT].current_duty = 0;
    g_motors[MOTOR_RIGHT].current_duty = 0;
    g_vacuum_on = false;
}
