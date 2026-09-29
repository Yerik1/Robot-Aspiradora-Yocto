/**
 * robot_selftest.c
 *
 * Prueba minima de librobot.so en el target (US-502), mientras el servidor
 * web no existe. Solo usa LEDs, un sensor y audio: NO comanda motores ni el
 * aspirado, para que sea seguro correrlo aunque el circuito ya este conectado.
 *
 * Sin circuito conectado, el sensor devuelve timeout (-5) y eso es lo
 * esperado. Debe correrse como root (pigpio necesita /dev/mem).
 */
#include <stdio.h>
#include <unistd.h>
#include "librobot.h"

int main(void)
{
    robot_status_t st = robot_init();
    printf("robot_init: %d\n", st);
    if (st != ROBOT_OK) {
        return 1;
    }

    for (int led = LED_AUTONOMOUS_MODE; led <= LED_SYSTEM_ON; led++) {
        printf("led_set(%d, on): %d\n", led, led_set((led_id_t)led, true));
    }
    sleep(1);
    for (int led = LED_AUTONOMOUS_MODE; led <= LED_SYSTEM_ON; led++) {
        led_set((led_id_t)led, false);
    }

    float dist = -1.0f;
    st = sensor_read_distance_cm(SENSOR_FRONT, &dist);
    printf("sensor_read_distance_cm(FRONT): %d, %.1f cm\n", st, dist);

    st = audio_play_notification(AUDIO_EVENT_SYSTEM_START);
    printf("audio_play_notification(SYSTEM_START): %d\n", st);
    sleep(2);

    printf("robot_cleanup: %d\n", robot_cleanup());
    return 0;
}
