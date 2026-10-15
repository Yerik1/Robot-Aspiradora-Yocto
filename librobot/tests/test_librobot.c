/**
 * test_librobot.c
 *
 * Arnes de prueba para librobot.so. Cubre los criterios de aceptacion
 * funcionales de:
 *
 *   US-301 (#15) "Implementar control de traccion diferencial por PWM":
 *     [1] Funciones que modifican el duty cycle de cada motor de forma
 *         independiente.                                    -> Seccion D
 *     [2] Soporte para avance, retroceso, giro izq/der y detencion.
 *                                                             -> Seccion E
 *     [3] Cambio de duty cycle produce cambio proporcional de velocidad.
 *                                                             -> Seccion D
 *     [4] Uso de la biblioteca GPIO/PWM documentado en el README.
 *         (No aplica a este script: es un item de documentacion, no de
 *         comportamiento en tiempo de ejecucion.)
 *
 *   US-302 (#17) "Implementar lectura de sensores de proximidad y control
 *   de LEDs":
 *     [1] Lectura de al menos 2 sensores (frontal y lateral) en cm.
 *                                                             -> Seccion F
 *     [2] Temporizacion trigger/echo correcta.                -> Seccion F
 *     [3] Control independiente de los 4 LEDs de estado.      -> Seccion G
 *     [4] Pruebas registradas mostrando encendido correcto ante eventos
 *         logicos esperados.                                  -> Seccion G
 *
 *   US-303 "Implementar reproduccion de audio (MP3) mediante mpg123 para
 *   notificaciones y musica de fondo":
 *     Reproduccion de archivo, notificaciones predefinidas, pausa/resume/
 *     stop con las transiciones de estado esperadas, control y consulta de
 *     volumen, consulta de estado y listado de pistas de musica de fondo.
 *                                                             -> Seccion H
 *
 * Ademas cubre, como base necesaria para que lo anterior sea confiable:
 *   Seccion A: guardas ROBOT_ERR_NOT_INITIALIZED antes de robot_init().
 *   Seccion B: robot_init() y su idempotencia.
 *   Seccion C: validacion de parametros invalidos en toda la API publica.
 *   Seccion I: robot_cleanup() y guardas posteriores al cleanup.
 *
 * Este mismo archivo se compila en DOS modos, elegidos por la macro
 * LIBROBOT_SIM_PROBES (la define sim/CMakeLists.txt; el CMakeLists.txt de
 * la raiz NUNCA la define):
 *
 *   1. Build de produccion (CMakeLists.txt de la raiz, contra la libpigpio
 *      real, pensado para correr en la Raspberry Pi con el hardware
 *      conectado). Los checks D/E/F/G que requieren observar el hardware
 *      (velocidad de una rueda, color de un LED) pausan y piden
 *      confirmacion "s/n" al operador -- la respuesta queda impresa con
 *      timestamp como evidencia (US-301.3 / US-302.4). Si stdin no es una
 *      terminal, esos pasos se marcan [SKIP] en vez de bloquear.
 *
 *        sudo ./build/test_librobot | tee run.log
 *
 *   2. Build de SIMULACION (sim/CMakeLists.txt, contra sim/pigpio_sim.c,
 *      pensado para correr en un host de desarrollo sin Raspberry Pi). Los
 *      mismos checks D/E/F/G se verifican de forma 100% automatica y no
 *      interactiva, leyendo el estado del hardware simulado a traves de
 *      sim/sim_probe.h (valor de PWM realmente aplicado, nivel de cada
 *      pin, distancia simulada de cada sensor) en vez de pedirle a un
 *      humano que mire. Esto prueba la LOGICA de motor.c/sensors.c/leds.c
 *      de punta a punta (matematica de rampa y compensacion optica,
 *      independencia entre motores/LEDs, maquina de estados de blink,
 *      timing trigger/echo, manejo de timeout) sin necesitar hardware.
 *
 *        cmake -S sim -B sim/build && cmake --build sim/build
 *        ./sim/build/test_librobot_sim
 *
 *      Lo que el modo simulado NO prueba: la senial electrica real en el
 *      pin fisico, ni que el motor/sensor/LED fisico responda como se
 *      espera -- para esa evidencia sigue haciendo falta el modo 1 en la
 *      Raspberry Pi real.
 *
 * Compilacion manual del build de produccion, si no se usa el CMakeLists
 * de la raiz:
 *   gcc -std=c11 -Wall -Wextra tests/test_librobot.c -Iinclude \
 *       -L build -lrobot -lpigpio -lpthread -o test_librobot
 */

/* localtime_r()/nanosleep() son POSIX, no C11 puro: se necesita esto para
 * que queden declarados incluso al compilar con -std=c11 estricto. */
#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "../include/librobot.h"

#ifdef LIBROBOT_SIM_PROBES
#include "../sim/sim_probe.h"
#include <math.h>
#endif

static int g_auto_pass = 0, g_auto_fail = 0;
#ifndef LIBROBOT_SIM_PROBES
static int g_manual_pass = 0, g_manual_fail = 0, g_manual_skip = 0;
#endif
static bool g_interactive;

/* usleep() quedo fuera de la exposicion de <unistd.h> con solo
 * _POSIX_C_SOURCE 200809L en glibc recientes (fue retirado de POSIX.1-2008);
 * nanosleep() es la alternativa portable y siempre visible con ese mismo
 * feature-test macro. */
static void sleep_ms(unsigned ms)
{
    struct timespec req;
    req.tv_sec = ms / 1000U;
    req.tv_nsec = (long)(ms % 1000U) * 1000000L;
    nanosleep(&req, NULL);
}

static void ts(char *buf, size_t n)
{
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    strftime(buf, n, "%H:%M:%S", &tmv);
}

static void section(const char *title)
{
    char b[16];
    ts(b, sizeof(b));
    printf("\n[%s] ==== %s ====\n", b, title);
}

static const char *status_name(robot_status_t s)
{
    switch (s) {
        case ROBOT_OK: return "ROBOT_OK";
        case ROBOT_ERR_NOT_INITIALIZED: return "ROBOT_ERR_NOT_INITIALIZED";
        case ROBOT_ERR_INVALID_PARAM: return "ROBOT_ERR_INVALID_PARAM";
        case ROBOT_ERR_GPIO: return "ROBOT_ERR_GPIO";
        case ROBOT_ERR_PWM: return "ROBOT_ERR_PWM";
        case ROBOT_ERR_SENSOR_TIMEOUT: return "ROBOT_ERR_SENSOR_TIMEOUT";
        case ROBOT_ERR_AUDIO: return "ROBOT_ERR_AUDIO";
        case ROBOT_ERR_AUDIO_FILE_NOT_FOUND: return "ROBOT_ERR_AUDIO_FILE_NOT_FOUND";
        default: return "?";
    }
}

/* Checks automaticos: no requieren que nadie mire el hardware, solo
 * comparar el codigo/valor devuelto por la API (o, en modo simulado, el
 * estado observado via sim_probe.h) contra lo esperado. */

static void check_status(const char *desc, robot_status_t got, robot_status_t expected)
{
    char b[16];
    ts(b, sizeof(b));
    if (got == expected) {
        g_auto_pass++;
        printf("[%s] [PASS] %s (%s)\n", b, desc, status_name(got));
    } else {
        g_auto_fail++;
        printf("[%s] [FAIL] %s -- esperado %s, obtenido %s\n",
               b, desc, status_name(expected), status_name(got));
    }
}

static void check_true(const char *desc, bool got)
{
    char b[16];
    ts(b, sizeof(b));
    if (got) {
        g_auto_pass++;
        printf("[%s] [PASS] %s\n", b, desc);
    } else {
        g_auto_fail++;
        printf("[%s] [FAIL] %s\n", b, desc);
    }
}

#ifdef LIBROBOT_SIM_PROBES
static void check_double_near(const char *desc, double got, double expected, double tol)
{
    char b[16];
    ts(b, sizeof(b));
    if (fabs(got - expected) <= tol) {
        g_auto_pass++;
        printf("[%s] [PASS] %s (valor=%.2f, esperado=%.2f +/- %.2f)\n", b, desc, got, expected, tol);
    } else {
        g_auto_fail++;
        printf("[%s] [FAIL] %s -- valor=%.2f, esperado=%.2f +/- %.2f\n", b, desc, got, expected, tol);
    }
}
#endif

#ifndef LIBROBOT_SIM_PROBES
/* Estas dos funciones solo tienen sentido en el build de produccion: en el
 * build simulado no hay nadie mirando el hardware, todo se verifica via
 * sim_probe.h (ver check_double_near/check_true mas arriba). */

/* Da tiempo al operador para reposicionarse/observar antes de seguir. Si no
 * hay nadie mirando (stdin no es tty), duerme un tiempo fijo en vez de
 * bloquear -- el hardware igual necesita ese tiempo para completar la
 * accion (p. ej. terminar un blink con duracion). */
static void wait_step(const char *msg, unsigned fallback_ms)
{
    if (g_interactive) {
        printf(">>> %s [Enter para continuar] ", msg);
        fflush(stdout);
        int c;
        while ((c = getchar()) != '\n' && c != EOF) { }
    } else {
        sleep_ms(fallback_ms);
    }
}

/* Pide confirmacion visual del operador y ANOTA el resultado como evidencia
 * manual (US-301.3 / US-302.4). */
static void confirm_manual(const char *desc)
{
    char b[16];
    if (!g_interactive) {
        ts(b, sizeof(b));
        g_manual_skip++;
        printf("[%s] [SKIP] %s (stdin no interactivo, no se puede confirmar visualmente)\n", b, desc);
        return;
    }

    printf(">>> %s ¿Correcto? (s/n): ", desc);
    fflush(stdout);
    char line[16] = {0};
    if (!fgets(line, sizeof(line), stdin)) {
        ts(b, sizeof(b));
        g_manual_skip++;
        printf("[%s] [SKIP] %s (sin respuesta)\n", b, desc);
        return;
    }
    ts(b, sizeof(b));
    if (line[0] == 's' || line[0] == 'S' || line[0] == 'y' || line[0] == 'Y') {
        g_manual_pass++;
        printf("[%s] [PASS-MANUAL] %s\n", b, desc);
    } else {
        g_manual_fail++;
        printf("[%s] [FAIL-MANUAL] %s\n", b, desc);
    }
}
#endif /* !LIBROBOT_SIM_PROBES */

/* ==========================================================================
 * Seccion A -- guardas antes de robot_init()
 * ========================================================================== */
static void section_a_pre_init_guards(void)
{
    section("A. Guardas antes de robot_init()");
    float d;
    uint8_t vol;
    audio_state_t audio_st;
    check_status("motor_set_speed() antes de init", motor_set_speed(MOTOR_LEFT, 50, MOTOR_DIR_FORWARD), ROBOT_ERR_NOT_INITIALIZED);
    check_status("motor_stop_all() antes de init", motor_stop_all(), ROBOT_ERR_NOT_INITIALIZED);
    check_status("vacuum_on() antes de init", vacuum_on(), ROBOT_ERR_NOT_INITIALIZED);
    check_status("sensor_read_distance_cm() antes de init", sensor_read_distance_cm(SENSOR_FRONT, &d), ROBOT_ERR_NOT_INITIALIZED);
    check_status("led_set() antes de init", led_set(LED_SYSTEM_ON, true), ROBOT_ERR_NOT_INITIALIZED);
    check_status("audio_play_file() antes de init", audio_play_file("/tmp/librobot_test_audio.mp3"), ROBOT_ERR_NOT_INITIALIZED);
    check_status("audio_pause() antes de init", audio_pause(), ROBOT_ERR_NOT_INITIALIZED);
    check_status("audio_get_volume() antes de init", audio_get_volume(&vol), ROBOT_ERR_NOT_INITIALIZED);
    check_status("audio_get_state() antes de init", audio_get_state(&audio_st), ROBOT_ERR_NOT_INITIALIZED);
}

/* ==========================================================================
 * Seccion B -- inicializacion
 * ========================================================================== */
static bool section_b_init(void)
{
    section("B. robot_init()");
    robot_status_t s = robot_init();
    check_status("robot_init() primera llamada", s, ROBOT_OK);
    if (s != ROBOT_OK) {
        printf("ABORTANDO: robot_init() fallo (%s). Revisar pigpio/permisos/hardware.\n", status_name(s));
        return false;
    }
    check_status("robot_init() llamada repetida es no-op", robot_init(), ROBOT_OK);
    return true;
}

/* ==========================================================================
 * Seccion C -- validacion de parametros invalidos
 * ========================================================================== */
static void section_c_param_validation(void)
{
    section("C. Validacion de parametros invalidos");
    float d;
    bool on;

    check_status("motor_set_speed() con motor_id invalido", motor_set_speed((motor_id_t)99, 50, MOTOR_DIR_FORWARD), ROBOT_ERR_INVALID_PARAM);
    check_status("motor_set_speed() con duty>100", motor_set_speed(MOTOR_LEFT, 101, MOTOR_DIR_FORWARD), ROBOT_ERR_INVALID_PARAM);
    check_status("motor_set_speed() con direccion invalida", motor_set_speed(MOTOR_LEFT, 50, (motor_direction_t)7), ROBOT_ERR_INVALID_PARAM);
    check_status("motor_stop() con motor_id invalido", motor_stop((motor_id_t)7), ROBOT_ERR_INVALID_PARAM);
    check_status("robot_move_forward() con speed>100", robot_move_forward(200), ROBOT_ERR_INVALID_PARAM);
    check_status("robot_move_backward() con speed>100", robot_move_backward(200), ROBOT_ERR_INVALID_PARAM);
    check_status("robot_turn_left() con radius>100", robot_turn_left(50, 150), ROBOT_ERR_INVALID_PARAM);
    check_status("robot_turn_right() con speed>100", robot_turn_right(150, 50), ROBOT_ERR_INVALID_PARAM);
    check_status("vacuum_is_on() con puntero NULL", vacuum_is_on(NULL), ROBOT_ERR_INVALID_PARAM);
    check_status("sensor_read_distance_cm() con sensor invalido", sensor_read_distance_cm((sensor_id_t)9, &d), ROBOT_ERR_INVALID_PARAM);
    check_status("sensor_read_distance_cm() con puntero NULL", sensor_read_distance_cm(SENSOR_FRONT, NULL), ROBOT_ERR_INVALID_PARAM);
    check_status("led_set() con led_id invalido", led_set((led_id_t)9, true), ROBOT_ERR_INVALID_PARAM);
    check_status("led_toggle() con led_id invalido", led_toggle((led_id_t)9), ROBOT_ERR_INVALID_PARAM);
    check_status("led_blink() con freq_hz=0", led_blink(LED_SYSTEM_ON, 0, 500), ROBOT_ERR_INVALID_PARAM);
    check_status("led_stop_blink() con led_id invalido", led_stop_blink((led_id_t)9), ROBOT_ERR_INVALID_PARAM);

    check_status("audio_play_file() con filepath NULL", audio_play_file(NULL), ROBOT_ERR_INVALID_PARAM);
    check_status("audio_play_file() con filepath vacio", audio_play_file(""), ROBOT_ERR_INVALID_PARAM);
    check_status("audio_play_notification() con evento invalido", audio_play_notification((audio_event_t)99), ROBOT_ERR_INVALID_PARAM);
    check_status("audio_set_volume() con volume>100", audio_set_volume(101), ROBOT_ERR_INVALID_PARAM);
    check_status("audio_get_volume() con puntero NULL", audio_get_volume(NULL), ROBOT_ERR_INVALID_PARAM);
    check_status("audio_get_state() con puntero NULL", audio_get_state(NULL), ROBOT_ERR_INVALID_PARAM);
    {
        char audio_paths[4][256];
        uint32_t audio_found;
        check_status("audio_list_files() con out_paths NULL", audio_list_files(NULL, 4, &audio_found), ROBOT_ERR_INVALID_PARAM);
        check_status("audio_list_files() con max_count=0", audio_list_files(audio_paths, 0, &audio_found), ROBOT_ERR_INVALID_PARAM);
        check_status("audio_list_files() con out_found NULL", audio_list_files(audio_paths, 4, NULL), ROBOT_ERR_INVALID_PARAM);
    }

    (void)on;
}

#ifdef LIBROBOT_SIM_PROBES
/* ==========================================================================
 * Seccion D (SIMULADA) -- duty cycle independiente + proporcionalidad
 * ========================================================================== */
static void assert_motor_state(const char *label, motor_id_t motor, uint8_t expected_duty, motor_direction_t expected_dir)
{
    char desc[192];
    double expected_pct = 100.0 - (double)expected_duty; /* compensacion optica de apply_en_duty() */

    snprintf(desc, sizeof(desc), "%s: PWM fisico refleja duty logico %u%% (compensado)", label, expected_duty);
    check_double_near(desc, sim_motor_pwm_percent(motor), expected_pct, 0.5);

    int in1 = -1, in2 = -1;
    int have_pins = sim_motor_dir_pins(motor, &in1, &in2);
    int expected_in1 = (expected_dir == MOTOR_DIR_FORWARD) ? 0 : 1;
    int expected_in2 = (expected_dir == MOTOR_DIR_FORWARD) ? 1 : 0;
    snprintf(desc, sizeof(desc), "%s: pines de direccion coinciden con %s (IN1=%d,IN2=%d)",
             label, expected_dir == MOTOR_DIR_FORWARD ? "FORWARD" : "BACKWARD", expected_in1, expected_in2);
    check_true(desc, have_pins == 0 && in1 == expected_in1 && in2 == expected_in2);
}

static void ramp_and_assert(motor_id_t motor, const char *motor_name, motor_id_t other, const char *other_name)
{
    const uint8_t duties[] = {25, 50, 75, 100};
    char desc[192];
    double other_before = sim_motor_pwm_percent(other);

    for (size_t i = 0; i < sizeof(duties) / sizeof(duties[0]); i++) {
        robot_status_t s = motor_set_speed(motor, duties[i], MOTOR_DIR_FORWARD);
        snprintf(desc, sizeof(desc), "motor_set_speed(%s, %u%%, FORWARD)", motor_name, duties[i]);
        check_status(desc, s, ROBOT_OK);
        assert_motor_state(motor_name, motor, duties[i], MOTOR_DIR_FORWARD);
    }

    snprintf(desc, sizeof(desc), "Motor %s no cambio mientras se variaba %s (independencia de canal PWM)", other_name, motor_name);
    check_double_near(desc, sim_motor_pwm_percent(other), other_before, 0.01);

    check_status("motor_stop() detiene el motor", motor_stop(motor), ROBOT_OK);
    assert_motor_state(motor_name, motor, 0, MOTOR_DIR_FORWARD);
}

static void section_d_independent_duty(void)
{
    section("D. Duty cycle independiente por motor y proporcionalidad de velocidad");

    printf("Prueba 1/2: variando MOTOR_LEFT, MOTOR_RIGHT debe permanecer sin cambios.\n");
    ramp_and_assert(MOTOR_LEFT, "IZQUIERDA", MOTOR_RIGHT, "DERECHA");

    printf("Prueba 2/2: variando MOTOR_RIGHT, MOTOR_LEFT debe permanecer sin cambios.\n");
    ramp_and_assert(MOTOR_RIGHT, "DERECHA", MOTOR_LEFT, "IZQUIERDA");

    check_status("motor_stop_all() al final de la seccion", motor_stop_all(), ROBOT_OK);
    assert_motor_state("IZQUIERDA", MOTOR_LEFT, 0, MOTOR_DIR_FORWARD);
    assert_motor_state("DERECHA", MOTOR_RIGHT, 0, MOTOR_DIR_FORWARD);
}

/* ==========================================================================
 * Seccion E (SIMULADA) -- avance/retroceso/giro/detencion
 * ========================================================================== */
static void section_e_movement(void)
{
    section("E. Avance, retroceso, giro y detencion");

    check_status("robot_move_forward(60)", robot_move_forward(60), ROBOT_OK);
    assert_motor_state("IZQUIERDA", MOTOR_LEFT, 60, MOTOR_DIR_FORWARD);
    assert_motor_state("DERECHA", MOTOR_RIGHT, 60, MOTOR_DIR_FORWARD);

    check_status("robot_move_backward(60)", robot_move_backward(60), ROBOT_OK);
    assert_motor_state("IZQUIERDA", MOTOR_LEFT, 60, MOTOR_DIR_BACKWARD);
    assert_motor_state("DERECHA", MOTOR_RIGHT, 60, MOTOR_DIR_BACKWARD);

    check_status("robot_turn_left(60, radius=0)", robot_turn_left(60, 0), ROBOT_OK);
    assert_motor_state("DERECHA (rueda exterior)", MOTOR_RIGHT, 60, MOTOR_DIR_FORWARD);
    assert_motor_state("IZQUIERDA (rueda interior, radio 0 = pivote)", MOTOR_LEFT, 60, MOTOR_DIR_BACKWARD);

    check_status("robot_turn_right(60, radius=0)", robot_turn_right(60, 0), ROBOT_OK);
    assert_motor_state("IZQUIERDA (rueda exterior)", MOTOR_LEFT, 60, MOTOR_DIR_FORWARD);
    assert_motor_state("DERECHA (rueda interior, radio 0 = pivote)", MOTOR_RIGHT, 60, MOTOR_DIR_BACKWARD);

    check_status("robot_turn_left(70, radius=50)", robot_turn_left(70, 50), ROBOT_OK);
    assert_motor_state("DERECHA (rueda exterior)", MOTOR_RIGHT, 70, MOTOR_DIR_FORWARD);
    assert_motor_state("IZQUIERDA (rueda interior, radio 50 => velocidad 0)", MOTOR_LEFT, 0, MOTOR_DIR_FORWARD);

    check_status("motor_stop_all() detiene ambos motores", motor_stop_all(), ROBOT_OK);
    assert_motor_state("IZQUIERDA", MOTOR_LEFT, 0, MOTOR_DIR_FORWARD);
    assert_motor_state("DERECHA", MOTOR_RIGHT, 0, MOTOR_DIR_FORWARD);

    bool on;
    check_status("vacuum_on()", vacuum_on(), ROBOT_OK);
    check_status("vacuum_is_on() refleja encendido", vacuum_is_on(&on), ROBOT_OK);
    check_true("vacuum_is_on() == true tras vacuum_on()", on);
    check_true("pin fisico de aspiradora en nivel ON (0, por inversion optica)", sim_vacuum_level() == 0);

    check_status("vacuum_off()", vacuum_off(), ROBOT_OK);
    check_status("vacuum_is_on() refleja apagado", vacuum_is_on(&on), ROBOT_OK);
    check_true("vacuum_is_on() == false tras vacuum_off()", !on);
    check_true("pin fisico de aspiradora en nivel OFF (1)", sim_vacuum_level() == 1);
}

/* ==========================================================================
 * Seccion F (SIMULADA) -- sensores de proximidad
 * ========================================================================== */
static void check_sensor_reads_near(sensor_id_t sensor, const char *name, double expected_cm)
{
    char desc[160];
    for (int i = 0; i < 3; i++) {
        float d = -1.0f;
        robot_status_t s = sensor_read_distance_cm(sensor, &d);
        snprintf(desc, sizeof(desc), "sensor_read_distance_cm(%s) muestra %d devuelve ROBOT_OK", name, i + 1);
        check_status(desc, s, ROBOT_OK);
        snprintf(desc, sizeof(desc), "sensor_read_distance_cm(%s) muestra %d ~= %.1fcm simulados", name, i + 1, expected_cm);
        check_double_near(desc, (double)d, expected_cm, 2.0);
        sleep_ms(80);
    }
}

static void section_f_sensors(void)
{
    section("F. Lectura de sensores de proximidad (frontal, izquierda, derecha)");

    sim_set_distance_cm(SENSOR_FRONT, 40.0);
    sim_set_distance_cm(SENSOR_LEFT, 25.0);
    sim_set_distance_cm(SENSOR_RIGHT, 10.0);

    check_sensor_reads_near(SENSOR_FRONT, "FRONT", 40.0);
    check_sensor_reads_near(SENSOR_LEFT, "LEFT", 25.0);
    check_sensor_reads_near(SENSOR_RIGHT, "RIGHT", 10.0);

    printf("Deteccion de obstaculo (temporizacion trigger/echo) -- sensor FRONTAL:\n");
    sim_set_distance_cm(SENSOR_FRONT, 8.0);
    check_true("sensor_obstacle_detected(FRONT,15cm) == true con objeto simulado a 8cm", sensor_obstacle_detected(SENSOR_FRONT, 15.0f));
    sim_set_distance_cm(SENSOR_FRONT, 40.0);
    check_true("sensor_obstacle_detected(FRONT,15cm) == false con via libre simulada a 40cm", !sensor_obstacle_detected(SENSOR_FRONT, 15.0f));

    printf("Deteccion de obstaculo -- sensor lateral IZQUIERDO:\n");
    sim_set_distance_cm(SENSOR_LEFT, 8.0);
    check_true("sensor_obstacle_detected(LEFT,15cm) == true con objeto simulado a 8cm", sensor_obstacle_detected(SENSOR_LEFT, 15.0f));
    sim_set_distance_cm(SENSOR_LEFT, 40.0);
    check_true("sensor_obstacle_detected(LEFT,15cm) == false con via libre simulada a 40cm", !sensor_obstacle_detected(SENSOR_LEFT, 15.0f));

    printf("Timeout de sensor (eco fuera de rango, ~%.0fcm > alcance del HC-SR04):\n", (35000.0 / 58.0));
    sim_set_distance_cm(SENSOR_FRONT, 2000.0);
    {
        float d;
        robot_status_t s = sensor_read_distance_cm(SENSOR_FRONT, &d);
        check_status("sensor_read_distance_cm(FRONT) con eco fuera de rango", s, ROBOT_ERR_SENSOR_TIMEOUT);
        check_true("sensor_obstacle_detected(FRONT) trata timeout como 'sin obstaculo' (por diseno, ver sensors.c)",
                   !sensor_obstacle_detected(SENSOR_FRONT, 15.0f));
    }
    sim_set_distance_cm(SENSOR_FRONT, 40.0);

    printf("Disparos consecutivos rapidos a los 3 sensores (separacion anti cross-talk, sin colgarse):\n");
    for (int i = 0; i < 6; i++) {
        float d;
        robot_status_t s1 = sensor_read_distance_cm(SENSOR_FRONT, &d);
        robot_status_t s2 = sensor_read_distance_cm(SENSOR_LEFT, &d);
        robot_status_t s3 = sensor_read_distance_cm(SENSOR_RIGHT, &d);
        check_true("lecturas consecutivas FRONT/LEFT/RIGHT no devuelven error inesperado",
                   (s1 == ROBOT_OK || s1 == ROBOT_ERR_SENSOR_TIMEOUT) &&
                   (s2 == ROBOT_OK || s2 == ROBOT_ERR_SENSOR_TIMEOUT) &&
                   (s3 == ROBOT_OK || s3 == ROBOT_ERR_SENSOR_TIMEOUT));
    }
}

/* ==========================================================================
 * Seccion G (SIMULADA) -- LEDs de estado
 * ========================================================================== */
static void assert_all_other_leds_off(led_id_t except)
{
    led_id_t all[] = { LED_AUTONOMOUS_MODE, LED_MANUAL_MODE, LED_OBSTACLE_ALERT, LED_SYSTEM_ON };
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        if (all[i] == except) continue;
        char desc[96];
        snprintf(desc, sizeof(desc), "LED %d permanece apagado mientras se controla otro", (int)all[i]);
        check_true(desc, sim_led_level(all[i]) != 1);
    }
}

static void section_g_leds(void)
{
    section("G. Control independiente de LEDs de estado");

    struct { led_id_t id; const char *name; } leds[] = {
        { LED_AUTONOMOUS_MODE, "AUTONOMOUS_MODE (D1)" },
        { LED_MANUAL_MODE,     "MANUAL_MODE (D2)" },
        { LED_OBSTACLE_ALERT,  "OBSTACLE_ALERT (D3)" },
        { LED_SYSTEM_ON,       "SYSTEM_ON (D4)" },
    };
    size_t n = sizeof(leds) / sizeof(leds[0]);

    printf("Encendido/apagado individual: los demas deben permanecer apagados.\n");
    for (size_t i = 0; i < n; i++) {
        char desc[128];
        check_status("led_set(led, true)", led_set(leds[i].id, true), ROBOT_OK);
        snprintf(desc, sizeof(desc), "Solo el LED %s esta encendido", leds[i].name);
        check_true(desc, sim_led_level(leds[i].id) == 1);
        assert_all_other_leds_off(leds[i].id);

        check_status("led_set(led, false)", led_set(leds[i].id, false), ROBOT_OK);
        snprintf(desc, sizeof(desc), "El LED %s quedo apagado", leds[i].name);
        check_true(desc, sim_led_level(leds[i].id) == 0);
    }

    printf("led_toggle():\n");
    check_status("led_toggle() enciende desde apagado", led_toggle(LED_SYSTEM_ON), ROBOT_OK);
    check_true("led_toggle() encendio el LED SYSTEM_ON", sim_led_level(LED_SYSTEM_ON) == 1);
    check_status("led_toggle() apaga desde encendido", led_toggle(LED_SYSTEM_ON), ROBOT_OK);
    check_true("led_toggle() apago el LED SYSTEM_ON", sim_led_level(LED_SYSTEM_ON) == 0);

    printf("led_blink() con duracion definida (5 Hz, 600 ms, autodetencion):\n");
    sim_led_reset_toggle_count(LED_OBSTACLE_ALERT);
    check_status("led_blink(OBSTACLE_ALERT, 5Hz, 600ms)", led_blink(LED_OBSTACLE_ALERT, 5, 600), ROBOT_OK);
    sleep_ms(800);
    check_true("El LED OBSTACLE_ALERT parpadeo un numero de veces consistente con 5Hz durante ~600ms",
               sim_led_toggle_count(LED_OBSTACLE_ALERT) >= 4);
    check_true("El LED OBSTACLE_ALERT quedo apagado tras autodetenerse", sim_led_level(LED_OBSTACLE_ALERT) == 0);

    printf("led_blink() indefinido + led_stop_blink():\n");
    sim_led_reset_toggle_count(LED_OBSTACLE_ALERT);
    check_status("led_blink(OBSTACLE_ALERT, 10Hz, indefinido)", led_blink(LED_OBSTACLE_ALERT, 10, 0), ROBOT_OK);
    sleep_ms(300);
    check_true("Hubo parpadeo real antes de detenerlo", sim_led_toggle_count(LED_OBSTACLE_ALERT) >= 2);
    check_status("led_stop_blink() detiene el parpadeo", led_stop_blink(LED_OBSTACLE_ALERT), ROBOT_OK);
    check_true("El LED quedo apagado tras led_stop_blink()", sim_led_level(LED_OBSTACLE_ALERT) == 0);

    printf("led_set() interrumpe un parpadeo en curso:\n");
    check_status("led_blink(MANUAL_MODE, 10Hz, indefinido)", led_blink(LED_MANUAL_MODE, 10, 0), ROBOT_OK);
    sleep_ms(150);
    check_status("led_set(MANUAL_MODE, true) fuerza encendido fijo", led_set(LED_MANUAL_MODE, true), ROBOT_OK);
    int toggles_right_after = sim_led_toggle_count(LED_MANUAL_MODE);
    sleep_ms(150);
    check_true("led_set() corto el parpadeo (sin transiciones nuevas tras forzar el nivel)",
               sim_led_toggle_count(LED_MANUAL_MODE) == toggles_right_after);
    check_true("El LED quedo fijo encendido", sim_led_level(LED_MANUAL_MODE) == 1);
    check_status("led_set(MANUAL_MODE, false) apaga", led_set(LED_MANUAL_MODE, false), ROBOT_OK);

    printf("\nEscenario de eventos logicos (US-302.4): simula la secuencia real del robot.\n");

    check_status("Evento: sistema encendido (SYSTEM_ON true)", led_set(LED_SYSTEM_ON, true), ROBOT_OK);
    check_true("Al 'encender el sistema', SYSTEM_ON esta encendido", sim_led_level(LED_SYSTEM_ON) == 1);
    assert_all_other_leds_off(LED_SYSTEM_ON);

    check_status("Evento: entra a modo autonomo (AUTONOMOUS_MODE true)", led_set(LED_AUTONOMOUS_MODE, true), ROBOT_OK);
    check_status("Evento: entra a modo autonomo (MANUAL_MODE false)", led_set(LED_MANUAL_MODE, false), ROBOT_OK);
    check_true("En modo autonomo: SYSTEM_ON y AUTONOMOUS_MODE encendidos",
               sim_led_level(LED_SYSTEM_ON) == 1 && sim_led_level(LED_AUTONOMOUS_MODE) == 1);
    check_true("En modo autonomo: MANUAL_MODE y OBSTACLE_ALERT apagados",
               sim_led_level(LED_MANUAL_MODE) == 0 && sim_led_level(LED_OBSTACLE_ALERT) == 0);

    sim_led_reset_toggle_count(LED_OBSTACLE_ALERT);
    check_status("Evento: obstaculo detectado (OBSTACLE_ALERT parpadea)", led_blink(LED_OBSTACLE_ALERT, 8, 0), ROBOT_OK);
    sleep_ms(300);
    check_true("Ante obstaculo detectado, OBSTACLE_ALERT esta parpadeando de verdad", sim_led_toggle_count(LED_OBSTACLE_ALERT) >= 2);
    check_true("AUTONOMOUS_MODE sigue encendido fijo durante la alerta", sim_led_level(LED_AUTONOMOUS_MODE) == 1);

    check_status("Evento: obstaculo despejado (se detiene el parpadeo)", led_stop_blink(LED_OBSTACLE_ALERT), ROBOT_OK);
    check_true("OBSTACLE_ALERT vuelve a apagado tras despejarse", sim_led_level(LED_OBSTACLE_ALERT) == 0);
    check_true("AUTONOMOUS_MODE sigue encendido", sim_led_level(LED_AUTONOMOUS_MODE) == 1);

    check_status("Evento: cambia a modo manual (AUTONOMOUS_MODE false)", led_set(LED_AUTONOMOUS_MODE, false), ROBOT_OK);
    check_status("Evento: cambia a modo manual (MANUAL_MODE true)", led_set(LED_MANUAL_MODE, true), ROBOT_OK);
    check_true("En modo manual: MANUAL_MODE encendido y AUTONOMOUS_MODE apagado",
               sim_led_level(LED_MANUAL_MODE) == 1 && sim_led_level(LED_AUTONOMOUS_MODE) == 0);
    check_true("SYSTEM_ON sigue encendido en modo manual", sim_led_level(LED_SYSTEM_ON) == 1);

    led_set(LED_MANUAL_MODE, false);
    led_set(LED_SYSTEM_ON, false);
}

#else /* !LIBROBOT_SIM_PROBES: build de produccion, contra hardware real */

/* ==========================================================================
 * Seccion D -- duty cycle independiente + proporcionalidad (US-301: 1 y 3)
 * ========================================================================== */
static void ramp_and_confirm(motor_id_t motor, const char *motor_name, const char *other_name)
{
    const uint8_t duties[] = {25, 50, 75, 100};
    char desc[160];

    for (size_t i = 0; i < sizeof(duties) / sizeof(duties[0]); i++) {
        robot_status_t s = motor_set_speed(motor, duties[i], MOTOR_DIR_FORWARD);
        snprintf(desc, sizeof(desc), "motor_set_speed(%s, %u%%, FORWARD)", motor_name, duties[i]);
        check_status(desc, s, ROBOT_OK);
        wait_step("Observa la rueda y compara con el paso anterior.", 600);
        snprintf(desc, sizeof(desc),
                 "Rueda %s gira mas rapido que en el paso anterior (duty=%u%%) y la rueda %s sigue detenida",
                 motor_name, duties[i], other_name);
        confirm_manual(desc);
    }
    check_status("motor_stop() detiene el motor", motor_stop(motor), ROBOT_OK);
}

static void section_d_independent_duty(void)
{
    section("D. Duty cycle independiente por motor y proporcionalidad de velocidad");

    printf("Prueba 1/2: variando MOTOR_LEFT, MOTOR_RIGHT debe permanecer detenido.\n");
    ramp_and_confirm(MOTOR_LEFT, "IZQUIERDA", "DERECHA");

    printf("Prueba 2/2: variando MOTOR_RIGHT, MOTOR_LEFT debe permanecer detenido.\n");
    ramp_and_confirm(MOTOR_RIGHT, "DERECHA", "IZQUIERDA");

    check_status("motor_stop_all() al final de la seccion", motor_stop_all(), ROBOT_OK);
}

/* ==========================================================================
 * Seccion E -- avance/retroceso/giro/detencion (US-301: 2)
 * ========================================================================== */
static void section_e_movement(void)
{
    section("E. Avance, retroceso, giro y detencion");

    check_status("robot_move_forward(60)", robot_move_forward(60), ROBOT_OK);
    wait_step("Observa: ambas ruedas deben girar hacia adelante.", 1000);
    confirm_manual("El robot avanza en linea recta");

    check_status("robot_move_backward(60)", robot_move_backward(60), ROBOT_OK);
    wait_step("Observa: ambas ruedas deben girar en reversa.", 1000);
    confirm_manual("El robot retrocede en linea recta");

    check_status("robot_turn_left(60, radius=0)", robot_turn_left(60, 0), ROBOT_OK);
    wait_step("Observa: giro sobre el propio eje hacia la izquierda.", 1000);
    confirm_manual("El robot gira sobre su eje hacia la izquierda (rueda izq. en reversa, derecha adelante)");

    check_status("robot_turn_right(60, radius=0)", robot_turn_right(60, 0), ROBOT_OK);
    wait_step("Observa: giro sobre el propio eje hacia la derecha.", 1000);
    confirm_manual("El robot gira sobre su eje hacia la derecha (rueda der. en reversa, izquierda adelante)");

    check_status("robot_turn_left(70, radius=50)", robot_turn_left(70, 50), ROBOT_OK);
    wait_step("Observa: giro amplio hacia la izquierda (curva, no pivote).", 1000);
    confirm_manual("El robot traza una curva amplia hacia la izquierda sin detenerse");

    check_status("motor_stop_all() detiene ambos motores", motor_stop_all(), ROBOT_OK);
    wait_step("Observa: ambas ruedas deben quedar quietas.", 500);
    confirm_manual("Ambas ruedas quedaron completamente detenidas");

    bool on;
    check_status("vacuum_on()", vacuum_on(), ROBOT_OK);
    check_status("vacuum_is_on() refleja encendido", vacuum_is_on(&on), ROBOT_OK);
    check_true("vacuum_is_on() == true tras vacuum_on()", on);
    wait_step("Observa/escucha el motor de aspirado.", 500);
    confirm_manual("El motor de aspirado esta encendido");

    check_status("vacuum_off()", vacuum_off(), ROBOT_OK);
    check_status("vacuum_is_on() refleja apagado", vacuum_is_on(&on), ROBOT_OK);
    check_true("vacuum_is_on() == false tras vacuum_off()", !on);
}

/* ==========================================================================
 * Seccion F -- sensores de proximidad (US-302: 1 y 2)
 * ========================================================================== */
static void read_sensor_samples(sensor_id_t sensor, const char *name)
{
    char desc[128];
    for (int i = 0; i < 5; i++) {
        float d = -1.0f;
        robot_status_t s = sensor_read_distance_cm(sensor, &d);
        char b[16];
        ts(b, sizeof(b));
        if (s == ROBOT_OK) {
            printf("[%s] [INFO] Sensor %s muestra %d: %.1f cm\n", b, name, i + 1, d);
        } else {
            printf("[%s] [INFO] Sensor %s muestra %d: sin eco (%s)\n", b, name, i + 1, status_name(s));
        }
        /* OK o timeout son ambos resultados validos en espacio abierto; lo
         * que NO es valido es cualquier otro codigo de error. */
        snprintf(desc, sizeof(desc), "sensor_read_distance_cm(%s) devuelve OK o TIMEOUT", name);
        check_true(desc, s == ROBOT_OK || s == ROBOT_ERR_SENSOR_TIMEOUT);
        sleep_ms(150);
    }
}

static void section_f_sensors(void)
{
    section("F. Lectura de sensores de proximidad (frontal, izquierda, derecha)");

    read_sensor_samples(SENSOR_FRONT, "FRONT");
    read_sensor_samples(SENSOR_LEFT, "LEFT");
    read_sensor_samples(SENSOR_RIGHT, "RIGHT");

    printf("Prueba de deteccion de obstaculo (temporizacion trigger/echo) -- sensor FRONTAL:\n");
    wait_step("Coloca un objeto a MENOS de 15 cm frente al sensor FRONTAL.", 1500);
    check_true("sensor_obstacle_detected(FRONT, 15cm) == true con objeto cerca", sensor_obstacle_detected(SENSOR_FRONT, 15.0f));
    wait_step("Retira el objeto de frente al sensor FRONTAL (deja > 30 cm libres).", 1500);
    check_true("sensor_obstacle_detected(FRONT, 15cm) == false sin objeto cerca", !sensor_obstacle_detected(SENSOR_FRONT, 15.0f));

    printf("Prueba de deteccion de obstaculo -- sensor lateral IZQUIERDO:\n");
    wait_step("Coloca un objeto a MENOS de 15 cm del sensor IZQUIERDO.", 1500);
    check_true("sensor_obstacle_detected(LEFT, 15cm) == true con objeto cerca", sensor_obstacle_detected(SENSOR_LEFT, 15.0f));
    wait_step("Retira el objeto de junto al sensor IZQUIERDO.", 1500);
    check_true("sensor_obstacle_detected(LEFT, 15cm) == false sin objeto cerca", !sensor_obstacle_detected(SENSOR_LEFT, 15.0f));

    printf("Disparos consecutivos rapidos a los 3 sensores (verifica separacion anti cross-talk, sin colgarse):\n");
    for (int i = 0; i < 6; i++) {
        float d;
        robot_status_t s1 = sensor_read_distance_cm(SENSOR_FRONT, &d);
        robot_status_t s2 = sensor_read_distance_cm(SENSOR_LEFT, &d);
        robot_status_t s3 = sensor_read_distance_cm(SENSOR_RIGHT, &d);
        check_true("lecturas consecutivas FRONT/LEFT/RIGHT no devuelven error inesperado",
                   (s1 == ROBOT_OK || s1 == ROBOT_ERR_SENSOR_TIMEOUT) &&
                   (s2 == ROBOT_OK || s2 == ROBOT_ERR_SENSOR_TIMEOUT) &&
                   (s3 == ROBOT_OK || s3 == ROBOT_ERR_SENSOR_TIMEOUT));
    }
}

/* ==========================================================================
 * Seccion G -- LEDs de estado (US-302: 3 y 4)
 * ========================================================================== */
static void section_g_leds(void)
{
    section("G. Control independiente de LEDs de estado");

    struct { led_id_t id; const char *name; } leds[] = {
        { LED_AUTONOMOUS_MODE, "AUTONOMOUS_MODE (D1)" },
        { LED_MANUAL_MODE,     "MANUAL_MODE (D2)" },
        { LED_OBSTACLE_ALERT,  "OBSTACLE_ALERT (D3)" },
        { LED_SYSTEM_ON,       "SYSTEM_ON (D4)" },
    };
    size_t n = sizeof(leds) / sizeof(leds[0]);

    printf("Encendido/apagado individual: todos los demas deben permanecer apagados.\n");
    for (size_t i = 0; i < n; i++) {
        char desc[128];
        check_status("led_set(led, true)", led_set(leds[i].id, true), ROBOT_OK);
        wait_step("Observa los 4 LEDs.", 500);
        snprintf(desc, sizeof(desc), "Solo el LED %s esta encendido", leds[i].name);
        confirm_manual(desc);
        check_status("led_set(led, false)", led_set(leds[i].id, false), ROBOT_OK);
        wait_step("Observa los 4 LEDs.", 300);
        snprintf(desc, sizeof(desc), "El LED %s quedo apagado y ningun otro se encendio", leds[i].name);
        confirm_manual(desc);
    }

    printf("led_toggle():\n");
    check_status("led_toggle() enciende desde apagado", led_toggle(LED_SYSTEM_ON), ROBOT_OK);
    wait_step("Observa el LED SYSTEM_ON.", 300);
    confirm_manual("led_toggle() encendio el LED SYSTEM_ON");
    check_status("led_toggle() apaga desde encendido", led_toggle(LED_SYSTEM_ON), ROBOT_OK);
    wait_step("Observa el LED SYSTEM_ON.", 300);
    confirm_manual("led_toggle() apago el LED SYSTEM_ON");

    printf("led_blink() con duracion definida (2 Hz, 3000 ms, autodetencion):\n");
    check_status("led_blink(OBSTACLE_ALERT, 2Hz, 3000ms)", led_blink(LED_OBSTACLE_ALERT, 2, 3000), ROBOT_OK);
    wait_step("Observa el LED OBSTACLE_ALERT parpadear ~2 veces por segundo.", 3300);
    confirm_manual("El LED OBSTACLE_ALERT parpadeo a ritmo visible y se detuvo solo tras ~3 s");

    printf("led_blink() indefinido + led_stop_blink():\n");
    check_status("led_blink(OBSTACLE_ALERT, 4Hz, indefinido)", led_blink(LED_OBSTACLE_ALERT, 4, 0), ROBOT_OK);
    wait_step("Observa el parpadeo continuo.", 1500);
    check_status("led_stop_blink() detiene el parpadeo", led_stop_blink(LED_OBSTACLE_ALERT), ROBOT_OK);
    wait_step("Observa que el LED quedo apagado y quieto.", 500);
    confirm_manual("led_stop_blink() detuvo el parpadeo y dejo el LED apagado");

    printf("led_set() interrumpe un parpadeo en curso:\n");
    check_status("led_blink(MANUAL_MODE, 3Hz, indefinido)", led_blink(LED_MANUAL_MODE, 3, 0), ROBOT_OK);
    wait_step("Observa el parpadeo.", 1000);
    check_status("led_set(MANUAL_MODE, true) fuerza encendido fijo", led_set(LED_MANUAL_MODE, true), ROBOT_OK);
    wait_step("Observa que el parpadeo se detuvo y quedo fijo encendido.", 500);
    confirm_manual("led_set() corto el parpadeo y dejo el LED fijo encendido");
    check_status("led_set(MANUAL_MODE, false) apaga", led_set(LED_MANUAL_MODE, false), ROBOT_OK);

    printf("\nEscenario de eventos logicos (US-302.4): simula la secuencia real del robot.\n");

    printf("Evento: sistema encendido.\n");
    check_status("led_set(SYSTEM_ON, true)", led_set(LED_SYSTEM_ON, true), ROBOT_OK);
    wait_step("Observa.", 300);
    confirm_manual("Al 'encender el sistema', solo SYSTEM_ON esta encendido");

    printf("Evento: entra a modo autonomo.\n");
    check_status("led_set(AUTONOMOUS_MODE, true)", led_set(LED_AUTONOMOUS_MODE, true), ROBOT_OK);
    check_status("led_set(MANUAL_MODE, false)", led_set(LED_MANUAL_MODE, false), ROBOT_OK);
    wait_step("Observa.", 300);
    confirm_manual("En 'modo autonomo', SYSTEM_ON y AUTONOMOUS_MODE encendidos; MANUAL_MODE y OBSTACLE_ALERT apagados");

    printf("Evento: obstaculo detectado en modo autonomo.\n");
    check_status("led_blink(OBSTACLE_ALERT, 5Hz, indefinido)", led_blink(LED_OBSTACLE_ALERT, 5, 0), ROBOT_OK);
    wait_step("Observa.", 1000);
    confirm_manual("Ante 'obstaculo detectado', OBSTACLE_ALERT parpadea mientras AUTONOMOUS_MODE sigue encendido fijo");

    printf("Evento: obstaculo despejado.\n");
    check_status("led_stop_blink(OBSTACLE_ALERT)", led_stop_blink(LED_OBSTACLE_ALERT), ROBOT_OK);
    wait_step("Observa.", 300);
    confirm_manual("Al 'despejarse el obstaculo', OBSTACLE_ALERT vuelve a apagado y AUTONOMOUS_MODE sigue encendido");

    printf("Evento: cambia a modo manual.\n");
    check_status("led_set(AUTONOMOUS_MODE, false)", led_set(LED_AUTONOMOUS_MODE, false), ROBOT_OK);
    check_status("led_set(MANUAL_MODE, true)", led_set(LED_MANUAL_MODE, true), ROBOT_OK);
    wait_step("Observa.", 300);
    confirm_manual("En 'modo manual', MANUAL_MODE encendido y AUTONOMOUS_MODE apagado; SYSTEM_ON sigue encendido");

    /* Deja todo apagado para la seccion de cleanup. */
    led_set(LED_MANUAL_MODE, false);
    led_set(LED_SYSTEM_ON, false);
}
#endif /* LIBROBOT_SIM_PROBES */

/* ==========================================================================
 * Seccion H -- reproduccion de audio (US-303)
 *
 * A diferencia de motor.c/sensors.c/leds.c, audio.c no toca GPIO/pigpio en
 * absoluto (lanza y habla con un proceso mpg123 real via pipe), asi que no
 * tiene contraparte simulada en sim_probe.h: este mismo checkeo automatico
 * corre igual en el build de produccion y en el simulado.
 * ========================================================================== */
static void section_h_audio(void)
{
    section("H. Reproduccion de audio (US-303)");

    const char *tmp_path = "/tmp/librobot_test_audio.mp3";
    FILE *f = fopen(tmp_path, "wb");
    check_true("Se pudo crear un archivo temporal para simular un MP3", f != NULL);
    if (f != NULL) {
        fwrite("ID3", 1, 3, f);
        fclose(f);
    }

    audio_state_t state;

    check_status("audio_play_file() con archivo inexistente", audio_play_file("/tmp/librobot_test_no_existe.mp3"), ROBOT_ERR_AUDIO_FILE_NOT_FOUND);

    check_status("audio_get_state() inicial", audio_get_state(&state), ROBOT_OK);
    check_true("Estado inicial de audio es AUDIO_STOPPED", state == AUDIO_STOPPED);

    check_status("audio_play_file() con archivo existente", audio_play_file(tmp_path), ROBOT_OK);
    check_status("audio_get_state() tras audio_play_file()", audio_get_state(&state), ROBOT_OK);
    check_true("Estado pasa a AUDIO_PLAYING tras audio_play_file()", state == AUDIO_PLAYING);

    check_status("audio_pause()", audio_pause(), ROBOT_OK);
    check_status("audio_get_state() tras audio_pause()", audio_get_state(&state), ROBOT_OK);
    check_true("Estado pasa a AUDIO_PAUSED tras audio_pause()", state == AUDIO_PAUSED);

    check_status("audio_resume()", audio_resume(), ROBOT_OK);
    check_status("audio_get_state() tras audio_resume()", audio_get_state(&state), ROBOT_OK);
    check_true("Estado vuelve a AUDIO_PLAYING tras audio_resume()", state == AUDIO_PLAYING);

    check_status("audio_stop()", audio_stop(), ROBOT_OK);
    check_status("audio_get_state() tras audio_stop()", audio_get_state(&state), ROBOT_OK);
    check_true("Estado vuelve a AUDIO_STOPPED tras audio_stop()", state == AUDIO_STOPPED);

    printf("audio_pause()/audio_resume() son no-op fuera de su estado esperado:\n");
    check_status("audio_pause() estando ya detenido no falla", audio_pause(), ROBOT_OK);
    check_status("audio_get_state() no cambia tras pause() sin reproduccion", audio_get_state(&state), ROBOT_OK);
    check_true("Estado sigue AUDIO_STOPPED (pause no aplica si no esta reproduciendo)", state == AUDIO_STOPPED);
    check_status("audio_resume() sin estar pausado no falla", audio_resume(), ROBOT_OK);
    check_status("audio_get_state() no cambia tras resume() sin pausa", audio_get_state(&state), ROBOT_OK);
    check_true("Estado sigue AUDIO_STOPPED (resume no aplica si no esta pausado)", state == AUDIO_STOPPED);

    printf("Control de volumen:\n");
    uint8_t vol;
    check_status("audio_set_volume(45)", audio_set_volume(45), ROBOT_OK);
    check_status("audio_get_volume() tras set(45)", audio_get_volume(&vol), ROBOT_OK);
    check_true("audio_get_volume() refleja 45", vol == 45);
    check_status("audio_set_volume(0)", audio_set_volume(0), ROBOT_OK);
    check_status("audio_get_volume() tras set(0)", audio_get_volume(&vol), ROBOT_OK);
    check_true("audio_get_volume() refleja 0", vol == 0);
    check_status("audio_set_volume(100)", audio_set_volume(100), ROBOT_OK);
    check_status("audio_get_volume() tras set(100)", audio_get_volume(&vol), ROBOT_OK);
    check_true("audio_get_volume() refleja 100", vol == 100);

    printf("Notificaciones predefinidas (US-303): los archivos del rootfs pueden no existir en\n"
           "este host de prueba, asi que se acepta ROBOT_OK o ROBOT_ERR_AUDIO_FILE_NOT_FOUND.\n");
    audio_event_t events[] = { AUDIO_EVENT_SYSTEM_START, AUDIO_EVENT_AUTONOMOUS_START, AUDIO_EVENT_OBSTACLE, AUDIO_EVENT_MANUAL_MODE };
    const char *event_names[] = { "SYSTEM_START", "AUTONOMOUS_START", "OBSTACLE", "MANUAL_MODE" };
    for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
        char desc[96];
        robot_status_t s = audio_play_notification(events[i]);
        snprintf(desc, sizeof(desc), "audio_play_notification(%s) devuelve OK o ARCHIVO_NO_ENCONTRADO", event_names[i]);
        check_true(desc, s == ROBOT_OK || s == ROBOT_ERR_AUDIO_FILE_NOT_FOUND);
    }
    audio_stop();

    printf("Listado de archivos de musica de fondo (US-303):\n");
    char paths[16][256];
    uint32_t found = 0;
    robot_status_t ls = audio_list_files(paths, 16, &found);
    check_true("audio_list_files() devuelve OK o ARCHIVO_NO_ENCONTRADO (directorio puede no existir en este host)",
               ls == ROBOT_OK || ls == ROBOT_ERR_AUDIO_FILE_NOT_FOUND);
    if (ls == ROBOT_OK) {
        check_true("audio_list_files() no reporta mas archivos que la capacidad del buffer", found <= 16);
        bool all_mp3 = true;
        for (uint32_t i = 0; i < found; i++) {
            size_t len = strlen(paths[i]);
            if (len <= 4 || strcasecmp(paths[i] + len - 4, ".mp3") != 0) {
                all_mp3 = false;
                break;
            }
        }
        check_true("Todos los archivos listados terminan en .mp3", all_mp3);
    }

    unlink(tmp_path);
}

/* ==========================================================================
 * Seccion I -- cleanup y guardas posteriores
 * ========================================================================== */
static void section_i_cleanup(void)
{
    section("I. robot_cleanup() y guardas posteriores");

    check_status("robot_cleanup()", robot_cleanup(), ROBOT_OK);

    float d;
    uint8_t vol;
    audio_state_t audio_st;
    check_status("motor_set_speed() despues de cleanup", motor_set_speed(MOTOR_LEFT, 50, MOTOR_DIR_FORWARD), ROBOT_ERR_NOT_INITIALIZED);
    check_status("sensor_read_distance_cm() despues de cleanup", sensor_read_distance_cm(SENSOR_FRONT, &d), ROBOT_ERR_NOT_INITIALIZED);
    check_status("led_set() despues de cleanup", led_set(LED_SYSTEM_ON, true), ROBOT_ERR_NOT_INITIALIZED);
    check_status("audio_play_file() despues de cleanup", audio_play_file("/tmp/librobot_test_audio.mp3"), ROBOT_ERR_NOT_INITIALIZED);
    check_status("audio_get_volume() despues de cleanup", audio_get_volume(&vol), ROBOT_ERR_NOT_INITIALIZED);
    check_status("audio_get_state() despues de cleanup", audio_get_state(&audio_st), ROBOT_ERR_NOT_INITIALIZED);
}

int main(void)
{
    g_interactive = isatty(fileno(stdin)) && isatty(fileno(stdout));

    printf("=========================================================\n");
    printf(" test_librobot -- arnes de prueba US-301 (#15) / US-302 (#17)\n");
#ifdef LIBROBOT_SIM_PROBES
    printf(" Modo: SIMULADO -- 100%% automatico, sin hardware ni operador\n");
#else
    printf(" Modo: %s\n", g_interactive ? "interactivo (con confirmacion visual)" : "no interactivo (checks fisicos se marcan SKIP)");
#endif
    printf("=========================================================\n");

    section_a_pre_init_guards();

    if (!section_b_init()) {
        return 1;
    }

    section_c_param_validation();
    section_d_independent_duty();
    section_e_movement();
    section_f_sensors();
    section_g_leds();
    section_h_audio();
    section_i_cleanup();

    section("RESUMEN");
    printf("Checks automaticos:   %d PASS / %d FAIL\n", g_auto_pass, g_auto_fail);
#ifndef LIBROBOT_SIM_PROBES
    printf("Checks manuales:      %d PASS / %d FAIL / %d SKIP\n", g_manual_pass, g_manual_fail, g_manual_skip);
#endif

#ifdef LIBROBOT_SIM_PROBES
    bool all_ok = (g_auto_fail == 0);
#else
    bool all_ok = (g_auto_fail == 0) && (g_manual_fail == 0);
#endif
    printf("Resultado global: %s\n", all_ok ? "PASS" : "FAIL");

#ifndef LIBROBOT_SIM_PROBES
    if (g_manual_skip > 0) {
        printf("Nota: %d check(s) fisico(s) quedaron sin confirmar (modo no interactivo).\n"
               "      Para dejar evidencia valida de US-301.3/US-302.4, correr en una\n"
               "      terminal interactiva sobre el hardware real.\n", g_manual_skip);
    }
#endif

    return all_ok ? 0 : 1;
}
