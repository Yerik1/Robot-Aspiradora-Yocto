/**
 * librobot.c
 *
 * robot_init() / robot_cleanup(): orquestan el arranque y apagado
 * ordenado de pigpio y de cada módulo (motor, sensores, LEDs).
 *
 * NOTA: el subsistema de audio (US-303, funciones audio_*() declaradas en
 * librobot.h) todavía no está implementado. robot_init() no lo inicializa
 * y robot_cleanup() no lo detiene -- pendiente para cuando se aborde
 * US-303.
 */
#include <pigpio.h>
#include "../include/librobot.h"
#include "internal.h"

/** Ver internal.h: gatea todas las funciones públicas de motor.c,
 *  sensors.c y leds.c. */
bool g_robot_initialized = false;

/** Separado de g_robot_initialized: indica si gpioInitialise() tuvo éxito,
 *  para que robot_cleanup() sepa si debe (y puede) llamar a gpioTerminate()
 *  incluso si la inicialización de algún módulo falló a medio camino. */
static bool g_pigpio_ready = false;

robot_status_t robot_init(void)
{
    if (g_robot_initialized) {
        return ROBOT_OK; /* ya inicializado; llamada repetida es no-op */
    }

    if (gpioInitialise() < 0) {
        return ROBOT_ERR_GPIO;
    }
    g_pigpio_ready = true;

    robot_status_t status;

    status = motor_module_init();
    if (status != ROBOT_OK) {
        robot_cleanup();
        return status;
    }

    status = sensor_module_init();
    if (status != ROBOT_OK) {
        robot_cleanup();
        return status;
    }

    status = led_module_init();
    if (status != ROBOT_OK) {
        robot_cleanup();
        return status;
    }

    status = audio_module_init();
    if (status != ROBOT_OK) {
        robot_cleanup();
        return status;
    }

    g_robot_initialized = true;
    return ROBOT_OK;
}

robot_status_t robot_cleanup(void)
{
    /* Se pone en false PRIMERO: a partir de este punto, cualquier llamada
     * concurrente a una función pública de motor.c/sensors.c/leds.c
     * devuelve ROBOT_ERR_NOT_INITIALIZED en vez de tocar hardware a medio
     * apagar. Por eso motor_module_cleanup()/led_module_cleanup() NO usan
     * las funciones públicas (motor_stop_all(), led_stop_blink()) -- esas
     * ya estarían bloqueadas por este mismo flag y no harían nada. */
    g_robot_initialized = false;

    if (g_pigpio_ready) {
        motor_module_cleanup();
        sensor_module_cleanup();
        led_module_cleanup();

        audio_module_cleanup();

        gpioTerminate();
        g_pigpio_ready = false;
    }

    return ROBOT_OK;
}
