/**
 * leds.c
 *
 * Implementación de US-302 (LEDs de estado): encendido directo, toggle, y
 * parpadeo no bloqueante en un hilo aparte.
 *
 * A diferencia de los pines de motor, los LEDs están conectados
 * directamente a GPIO en el dominio "rasp gnd" (no pasan por
 * optoacoplador), así que no hay inversión de nivel que compensar aquí:
 * GPIO alto = LED encendido.
 */
#include <pigpio.h>
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include "../include/librobot.h"
#include "internal.h"
#include "robot_pins.h"

typedef struct {
    int pin;
    bool state;                 /* true = encendido */
    bool blink_active;          /* señal para que el hilo de parpadeo pare */
    pthread_t blink_thread;
    bool blink_thread_running;  /* true si hay un hilo detached en curso */
    pthread_mutex_t lock;
} led_ctx_t;

static led_ctx_t g_leds[4] = {
    [LED_AUTONOMOUS_MODE] = { .pin = PIN_LED_AUTONOMOUS_MODE, .lock = PTHREAD_MUTEX_INITIALIZER },
    [LED_MANUAL_MODE]     = { .pin = PIN_LED_MANUAL_MODE,     .lock = PTHREAD_MUTEX_INITIALIZER },
    [LED_OBSTACLE_ALERT]  = { .pin = PIN_LED_OBSTACLE_ALERT,  .lock = PTHREAD_MUTEX_INITIALIZER },
    [LED_SYSTEM_ON]       = { .pin = PIN_LED_SYSTEM_ON,       .lock = PTHREAD_MUTEX_INITIALIZER },
};

/* Tiempo que led_stop_blink()/led_set() esperan a que el hilo de parpadeo
 * note blink_active==false y termine, antes de devolver el control. Es un
 * límite superior bajo (el hilo revisa la bandera en cada medio periodo,
 * como mucho); no bloquea indefinidamente. */
#define BLINK_STOP_GRACE_US 20000

typedef struct {
    led_ctx_t *led;
    uint32_t half_period_us;
    uint32_t duration_ms; /* 0 = indefinido */
} blink_args_t;

static void *blink_thread_fn(void *arg)
{
    blink_args_t *args = (blink_args_t *)arg;
    led_ctx_t *led = args->led;
    uint32_t half_period_us = args->half_period_us;
    uint32_t duration_ms = args->duration_ms;

    uint32_t elapsed_us = 0;
    bool level = true;

    for (;;) {
        pthread_mutex_lock(&led->lock);
        bool still_active = led->blink_active;
        pthread_mutex_unlock(&led->lock);

        if (!still_active) break;
        if (duration_ms != 0 && (elapsed_us / 1000) >= duration_ms) break;

        gpioWrite(led->pin, level ? 1 : 0);
        led->state = level;
        level = !level;

        gpioDelay(half_period_us);
        elapsed_us += half_period_us;
    }

    pthread_mutex_lock(&led->lock);
    led->blink_active = false;
    led->blink_thread_running = false;
    pthread_mutex_unlock(&led->lock);

    free(args);
    return NULL;
}

static robot_status_t start_blink_thread(led_ctx_t *led, uint32_t freq_hz, uint32_t duration_ms)
{
    blink_args_t *args = malloc(sizeof(blink_args_t));
    if (args == NULL) return ROBOT_ERR_GPIO;

    args->led = led;
    args->half_period_us = (uint32_t)(500000.0 / (double)freq_hz);
    args->duration_ms = duration_ms;

    led->blink_active = true;
    led->blink_thread_running = true;

    pthread_t tid;
    if (pthread_create(&tid, NULL, blink_thread_fn, args) != 0) {
        led->blink_active = false;
        led->blink_thread_running = false;
        free(args);
        return ROBOT_ERR_GPIO;
    }
    pthread_detach(tid);

    return ROBOT_OK;
}

/** Detiene cualquier parpadeo activo de un LED y espera un tiempo acotado
 *  a que el hilo termine, sin llamar a la API pública (para poder usarse
 *  también desde el cleanup del módulo). */
static void stop_blink_internal(led_ctx_t *led)
{
    pthread_mutex_lock(&led->lock);
    bool was_running = led->blink_thread_running;
    led->blink_active = false;
    pthread_mutex_unlock(&led->lock);

    if (was_running) {
        gpioDelay(BLINK_STOP_GRACE_US);
    }
}

/* ==========================================================================
 * API pública -- US-302
 * ========================================================================== */

robot_status_t led_set(led_id_t led, bool on)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;
    if (led < LED_AUTONOMOUS_MODE || led > LED_SYSTEM_ON) return ROBOT_ERR_INVALID_PARAM;

    led_ctx_t *ctx = &g_leds[led];
    stop_blink_internal(ctx);

    gpioWrite(ctx->pin, on ? 1 : 0);
    ctx->state = on;

    return ROBOT_OK;
}

robot_status_t led_toggle(led_id_t led)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;
    if (led < LED_AUTONOMOUS_MODE || led > LED_SYSTEM_ON) return ROBOT_ERR_INVALID_PARAM;

    led_ctx_t *ctx = &g_leds[led];
    stop_blink_internal(ctx);

    bool new_state = !ctx->state;
    gpioWrite(ctx->pin, new_state ? 1 : 0);
    ctx->state = new_state;

    return ROBOT_OK;
}

robot_status_t led_blink(led_id_t led, uint32_t freq_hz, uint32_t duration_ms)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;
    if (led < LED_AUTONOMOUS_MODE || led > LED_SYSTEM_ON) return ROBOT_ERR_INVALID_PARAM;
    if (freq_hz == 0) return ROBOT_ERR_INVALID_PARAM;

    led_ctx_t *ctx = &g_leds[led];
    stop_blink_internal(ctx);

    return start_blink_thread(ctx, freq_hz, duration_ms);
}

robot_status_t led_stop_blink(led_id_t led)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;
    if (led < LED_AUTONOMOUS_MODE || led > LED_SYSTEM_ON) return ROBOT_ERR_INVALID_PARAM;

    led_ctx_t *ctx = &g_leds[led];
    stop_blink_internal(ctx);

    gpioWrite(ctx->pin, 0);
    ctx->state = false;

    return ROBOT_OK;
}

/* ==========================================================================
 * Init / cleanup internos (llamados por librobot.c)
 * ========================================================================== */

robot_status_t led_module_init(void)
{
    led_id_t ids[] = { LED_AUTONOMOUS_MODE, LED_MANUAL_MODE, LED_OBSTACLE_ALERT, LED_SYSTEM_ON };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        led_ctx_t *ctx = &g_leds[ids[i]];
        if (gpioSetMode(ctx->pin, PI_OUTPUT) != 0) return ROBOT_ERR_GPIO;
        gpioWrite(ctx->pin, 0);
        ctx->state = false;
        ctx->blink_active = false;
        ctx->blink_thread_running = false;
    }
    return ROBOT_OK;
}

/**
 * IMPORTANTE: al igual que motor_module_cleanup(), esta función corre
 * DESPUÉS de que g_robot_initialized ya es false, así que manipula el
 * estado directamente (vía stop_blink_internal(), que no depende de ese
 * flag) en vez de llamar a la API pública led_stop_blink(), que sí lo
 * revisa y haría un no-op silencioso dejando algún LED parpadeando.
 */
void led_module_cleanup(void)
{
    led_id_t ids[] = { LED_AUTONOMOUS_MODE, LED_MANUAL_MODE, LED_OBSTACLE_ALERT, LED_SYSTEM_ON };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        led_ctx_t *ctx = &g_leds[ids[i]];
        stop_blink_internal(ctx);
        gpioWrite(ctx->pin, 0);
        ctx->state = false;
    }
}
