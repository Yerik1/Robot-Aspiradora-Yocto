/**
 * robot_selftest.c
 *
 * Prueba minima de librobot.so en el target. Sin argumentos solo usa
 * LEDs, sensores y audio. Los modos "motores" y "aspirado" SI mueven
 * actuadores. Debe correrse como root.
 *
 * Uso:
 *   robot-selftest            secuencia completa (LEDs, sensores, audio)
 *   robot-selftest sensores   20 lecturas de sensores, una cada 0.5 s
 *   robot-selftest audio [archivo.mp3]
 *                             prueba de musica: lista, reproduce, volumen,
 *                             pausa, reanuda y detiene (~35 s)
 *   robot-selftest motores    cada motor por separado y movimientos
 *                             compuestos (~40 s). RUEDAS EN EL AIRE.
 *   robot-selftest aspirado   motor de aspirado 3 s
 *
 * Ctrl+C durante la prueba detiene motores y aspirado antes de salir.
 *
 * Secuencia de LEDs (1 s cada uno):
 *   sistema (blanco) -> autonomo (verde) -> manual (azul) -> obstaculo (rojo)
 *   -> los cuatro juntos -> parpadeo del LED de obstaculo (2 Hz, 3 s)
 */
#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "librobot.h"

#define SENSOR_LOOP_COUNT     20
#define SENSOR_LOOP_GAP_MS   500
#define AUDIO_LIST_MAX        16
#define MOTOR_TEST_SPEED      60
#define MOTOR_STEP_MS       2000
#define MOTOR_PAUSE_MS      1000

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

/* Reemplaza el manejador de pigpio para que Ctrl+C pase por robot_cleanup
 * y los motores queden detenidos. */
static void install_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

static const char *led_name[] = { "autonomo", "manual", "obstaculo", "sistema" };

static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void print_sensors(void)
{
    float dist = -1.0f;
    bool left = false;
    bool right = false;

    robot_status_t sf = sensor_read_distance_cm(SENSOR_FRONT, &dist);
    robot_status_t sl = sensor_read_obstacle(SENSOR_LEFT, 0.0f, &left);
    robot_status_t sr = sensor_read_obstacle(SENSOR_RIGHT, 0.0f, &right);

    if (sf == ROBOT_OK) {
        printf("FRONT: %6.1f cm", dist);
    } else {
        printf("FRONT: sin eco (%d)", sf);
    }
    printf(" | LEFT: %s (%d) | RIGHT: %s (%d)\n",
           left ? "OBSTACULO" : "libre", sl,
           right ? "OBSTACULO" : "libre", sr);
}

static void sensor_loop(void)
{
    for (int i = 0; i < SENSOR_LOOP_COUNT; i++) {
        printf("%2d  ", i + 1);
        print_sensors();
        fflush(stdout);
        sleep_ms(SENSOR_LOOP_GAP_MS);
    }
}

/* Espera ms milisegundos; devuelve false si se pidió detener. */
static bool wait_ms(long ms)
{
    while (ms > 0 && !g_stop) {
        long step = ms > 50 ? 50 : ms;
        sleep_ms(step);
        ms -= step;
    }
    return !g_stop;
}

/* Ejecuta un paso de motores: imprime el resultado, espera y detiene. */
static bool motor_step(const char *name, robot_status_t st)
{
    printf("%-34s %d\n", name, st);
    fflush(stdout);
    bool ok = wait_ms(MOTOR_STEP_MS);
    motor_stop_all();
    return ok && wait_ms(MOTOR_PAUSE_MS);
}

static void motor_test(void)
{
    const uint8_t v = MOTOR_TEST_SPEED;

    printf("== motores individuales (velocidad %u%%) ==\n", (unsigned)v);
    if (!motor_step("izquierdo adelante",  motor_set_speed(MOTOR_LEFT,  v, MOTOR_DIR_FORWARD)))  return;
    if (!motor_step("izquierdo atras",     motor_set_speed(MOTOR_LEFT,  v, MOTOR_DIR_BACKWARD))) return;
    if (!motor_step("derecho adelante",    motor_set_speed(MOTOR_RIGHT, v, MOTOR_DIR_FORWARD)))  return;
    if (!motor_step("derecho atras",       motor_set_speed(MOTOR_RIGHT, v, MOTOR_DIR_BACKWARD))) return;

    printf("== movimientos compuestos ==\n");
    if (!motor_step("robot_move_forward",        robot_move_forward(v)))      return;
    if (!motor_step("robot_move_backward",       robot_move_backward(v)))     return;
    if (!motor_step("robot_turn_left (eje)",     robot_turn_left(v, 0)))      return;
    if (!motor_step("robot_turn_right (eje)",    robot_turn_right(v, 0)))     return;
    if (!motor_step("robot_turn_left (amplio)",  robot_turn_left(v, 100)))    return;

    printf("== rampa de velocidad, ambos adelante ==\n");
    const uint8_t speeds[] = { 30, 60, 100 };
    for (int i = 0; i < 3; i++) {
        char label[40];
        snprintf(label, sizeof(label), "robot_move_forward(%u)", (unsigned)speeds[i]);
        if (!motor_step(label, robot_move_forward(speeds[i]))) return;
    }

    printf("motor_stop_all: %d\n", motor_stop_all());
}

static void vacuum_test(void)
{
    printf("vacuum_on: %d\n", vacuum_on());
    bool on = false;
    vacuum_is_on(&on);
    printf("    vacuum_is_on: %s\n", on ? "si" : "no");
    wait_ms(3000);
    printf("vacuum_off: %d\n", vacuum_off());
    vacuum_is_on(&on);
    printf("    vacuum_is_on: %s\n", on ? "si" : "no");
}

static const char *state_name(audio_state_t s)
{
    switch (s) {
    case AUDIO_PLAYING: return "PLAYING";
    case AUDIO_PAUSED:  return "PAUSED";
    default:            return "STOPPED";
    }
}

static void print_audio_state(void)
{
    audio_state_t s = AUDIO_STOPPED;
    uint8_t vol = 0;
    audio_get_state(&s);
    audio_get_volume(&vol);
    printf("    estado: %s, volumen: %u\n", state_name(s), (unsigned)vol);
}

static void audio_test(const char *forced_file)
{
    char files[AUDIO_LIST_MAX][256];
    uint32_t found = 0;

    robot_status_t st = audio_list_files(files, AUDIO_LIST_MAX, &found);
    printf("audio_list_files: %d, %u archivo(s)\n", st, (unsigned)found);
    for (uint32_t i = 0; i < found; i++) {
        printf("  [%u] %s\n", (unsigned)i, files[i]);
    }

    const char *first = forced_file;
    if (first == NULL) {
        if (found == 0) {
            printf("no hay canciones para probar\n");
            return;
        }
        first = files[0];
    }

    printf("audio_play_file(%s): %d\n", first, audio_play_file(first));
    print_audio_state();
    sleep(6);

    printf("audio_set_volume(30): %d\n", audio_set_volume(30));
    print_audio_state();
    sleep(4);

    printf("audio_set_volume(100): %d\n", audio_set_volume(100));
    print_audio_state();
    sleep(4);

    printf("audio_pause: %d\n", audio_pause());
    print_audio_state();
    sleep(3);

    printf("audio_resume: %d\n", audio_resume());
    print_audio_state();
    sleep(5);

    if (forced_file == NULL && found > 1) {
        printf("audio_play_file(%s): %d\n", files[1], audio_play_file(files[1]));
        print_audio_state();
        sleep(6);
    }

    printf("audio_stop: %d\n", audio_stop());
    print_audio_state();
    sleep(1);

    printf("audio_play_file(/no/existe.mp3): %d (esperado %d)\n",
           audio_play_file("/no/existe.mp3"), ROBOT_ERR_AUDIO_FILE_NOT_FOUND);
}

static void led_sequence(void)
{
    const led_id_t order[] = { LED_SYSTEM_ON, LED_AUTONOMOUS_MODE,
                               LED_MANUAL_MODE, LED_OBSTACLE_ALERT };
    for (int i = 0; i < 4; i++) {
        printf("led_set(%s, on): %d\n", led_name[order[i]], led_set(order[i], true));
        sleep(1);
        led_set(order[i], false);
    }

    printf("los cuatro LEDs encendidos\n");
    for (int i = 0; i < 4; i++) {
        led_set(order[i], true);
    }
    sleep(2);
    for (int i = 0; i < 4; i++) {
        led_set(order[i], false);
    }

    printf("led_blink(obstaculo, 2 Hz, 3000 ms): %d\n",
           led_blink(LED_OBSTACLE_ALERT, 2, 3000));
    sleep(4);
}

int main(int argc, char **argv)
{
    robot_status_t st = robot_init();
    printf("robot_init: %d\n", st);
    if (st != ROBOT_OK) {
        return 1;
    }
    install_signal_handlers();

    if (argc > 1 && strcmp(argv[1], "motores") == 0) {
        motor_test();
    } else if (argc > 1 && strcmp(argv[1], "aspirado") == 0) {
        vacuum_test();
    } else if (argc > 1 && strcmp(argv[1], "sensores") == 0) {
        sensor_loop();
    } else if (argc > 1 && strcmp(argv[1], "audio") == 0) {
        audio_test(argc > 2 ? argv[2] : NULL);
    } else {
        led_sequence();
        print_sensors();
        st = audio_play_notification(AUDIO_EVENT_SYSTEM_START);
        printf("audio_play_notification(SYSTEM_START): %d\n", st);
        sleep(2);
    }

    if (g_stop) {
        printf("\ninterrumpido: deteniendo motores\n");
    }
    motor_stop_all();
    vacuum_off();
    printf("robot_cleanup: %d\n", robot_cleanup());
    return 0;
}
