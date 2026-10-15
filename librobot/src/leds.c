/**
 * leds.c
 *
 * Indicadores de estado sobre una tira de 4 LEDs SK6812MINI-E controlada
 * por SPI (ver robot_pins.h). Cada led_id_t de la API corresponde a un
 * tramo de la tira con su propio color, de modo que la API pública
 * (led_set, led_toggle, led_blink, led_stop_blink) no cambia.
 *
 * Patrón de trabajo: el estado lógico se pinta en un búfer de colores y
 * el búfer completo se envía a la tira en una sola transferencia SPI.
 *
 * Compilar con -DLIBROBOT_SIM para probar en una PC sin /dev/spidev: en
 * ese modo las transferencias se omiten.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#include "../include/librobot.h"
#include "internal.h"
#include "robot_pins.h"

#define LED_COUNT_LOGICAL   4
#define SPI_BYTES_PER_LED   12      /* 3 colores x 8 bits x 4 bits SPI */
#define SPI_LEAD_BYTES      1       /* asegura MOSI en bajo al inicio  */
#define SPI_RESET_BYTES     48      /* 48 x 2.5 us = 120 us en bajo    */
#define SPI_FRAME_BYTES     (SPI_LEAD_BYTES + LED_STRIP_COUNT * SPI_BYTES_PER_LED + SPI_RESET_BYTES)
#define BLINK_TICK_MS       10

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} LedColor;

typedef struct {
    int start;
    int count;
    LedColor color;
} LedZone;

typedef struct {
    bool on;
    bool blinking;
    uint32_t freq_hz;
    uint64_t blink_start_ms;
    uint64_t blink_end_ms;      /* 0 = indefinido */
} LedState;

/* Índice = led_id_t. Colores con brillo limitado por LED_STRIP_MAX_LEVEL. */
static const LedZone g_zones[LED_COUNT_LOGICAL] = {
    [LED_AUTONOMOUS_MODE] = { LED_ZONE_AUTONOMOUS_START, LED_ZONE_AUTONOMOUS_COUNT,
                              { 0, LED_STRIP_MAX_LEVEL, 0 } },                  /* verde    */
    [LED_MANUAL_MODE]     = { LED_ZONE_MANUAL_START, LED_ZONE_MANUAL_COUNT,
                              { 0, 0, LED_STRIP_MAX_LEVEL } },                  /* azul     */
    [LED_OBSTACLE_ALERT]  = { LED_ZONE_OBSTACLE_START, LED_ZONE_OBSTACLE_COUNT,
                              { LED_STRIP_MAX_LEVEL, 0, 0 } },                  /* rojo     */
    [LED_SYSTEM_ON]       = { LED_ZONE_SYSTEM_ON_START, LED_ZONE_SYSTEM_ON_COUNT,
                              { LED_STRIP_MAX_LEVEL / 2, LED_STRIP_MAX_LEVEL / 2,
                                LED_STRIP_MAX_LEVEL / 2 } },                    /* blanco   */
};

static pthread_mutex_t g_led_lock = PTHREAD_MUTEX_INITIALIZER;
static LedState g_state[LED_COUNT_LOGICAL];
static LedColor g_pixels[LED_STRIP_COUNT];
static uint8_t g_frame[SPI_FRAME_BYTES];
static int g_spi_fd = -1;

static pthread_t g_blink_thread;
static bool g_blink_thread_running = false;
static bool g_blink_thread_stop = false;

/* ========================================================================= */

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* Un byte de color -> 4 bytes SPI (cada bit del LED ocupa 4 bits SPI). */
static void encode_byte(uint8_t value, uint8_t *out)
{
    for (int i = 0; i < 4; i++) {
        int bit_hi = (value >> (7 - 2 * i)) & 1;
        int bit_lo = (value >> (6 - 2 * i)) & 1;
        uint8_t nib_hi = bit_hi ? 0xC : 0x8;   /* 1100 o 1000 */
        uint8_t nib_lo = bit_lo ? 0xC : 0x8;
        out[i] = (uint8_t)((nib_hi << 4) | nib_lo);
    }
}

/* Pinta los indicadores encendidos sobre el búfer de colores. */
static void render_pixels(void)
{
    memset(g_pixels, 0, sizeof(g_pixels));
    for (int led = 0; led < LED_COUNT_LOGICAL; led++) {
        if (!g_state[led].on) {
            continue;
        }
        const LedZone *z = &g_zones[led];
        for (int i = z->start; i < z->start + z->count && i < LED_STRIP_COUNT; i++) {
            g_pixels[i] = z->color;
        }
    }
}

/* Envía el búfer completo a la tira. Debe llamarse con g_led_lock tomado. */
static robot_status_t flush_strip(void)
{
    render_pixels();

    memset(g_frame, 0, sizeof(g_frame));
    uint8_t *p = g_frame + SPI_LEAD_BYTES;
    for (int i = 0; i < LED_STRIP_COUNT; i++) {
        /* SK6812: orden de envío G, R, B */
        encode_byte(g_pixels[i].g, p);     p += 4;
        encode_byte(g_pixels[i].r, p);     p += 4;
        encode_byte(g_pixels[i].b, p);     p += 4;
    }

#ifdef LIBROBOT_SIM
    return ROBOT_OK;
#else
    if (g_spi_fd < 0) {
        return ROBOT_ERR_GPIO;
    }
    struct spi_ioc_transfer tr;
    memset(&tr, 0, sizeof(tr));
    tr.tx_buf = (unsigned long)g_frame;
    tr.len = sizeof(g_frame);
    tr.speed_hz = LED_STRIP_SPI_HZ;
    tr.bits_per_word = 8;

    if (ioctl(g_spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        return ROBOT_ERR_GPIO;
    }
    return ROBOT_OK;
#endif
}

/* Hilo que actualiza los LEDs en parpadeo. */
static void *blink_thread_fn(void *arg)
{
    (void)arg;
    struct timespec tick = { 0, BLINK_TICK_MS * 1000000L };

    for (;;) {
        pthread_mutex_lock(&g_led_lock);
        if (g_blink_thread_stop) {
            pthread_mutex_unlock(&g_led_lock);
            break;
        }

        bool changed = false;
        uint64_t t = now_ms();
        for (int led = 0; led < LED_COUNT_LOGICAL; led++) {
            LedState *s = &g_state[led];
            if (!s->blinking) {
                continue;
            }
            bool next_on;
            if (s->blink_end_ms != 0 && t >= s->blink_end_ms) {
                s->blinking = false;
                next_on = false;            /* al terminar queda apagado */
            } else {
                uint64_t half_period_ms = 500u / s->freq_hz;
                if (half_period_ms == 0) {
                    half_period_ms = 1;
                }
                next_on = (((t - s->blink_start_ms) / half_period_ms) % 2u) == 0;
            }
            if (next_on != s->on) {
                s->on = next_on;
                changed = true;
            }
        }
        if (changed) {
            flush_strip();
        }
        pthread_mutex_unlock(&g_led_lock);

        nanosleep(&tick, NULL);
    }
    return NULL;
}

/* ========================================================================= */
/* Inicialización / limpieza (invocadas por robot_init / robot_cleanup)      */
/* ========================================================================= */

robot_status_t led_module_init(void)
{
    memset(g_state, 0, sizeof(g_state));

#ifndef LIBROBOT_SIM
    g_spi_fd = open(LED_STRIP_SPI_DEVICE, O_RDWR);
    if (g_spi_fd < 0) {
        fprintf(stderr, "librobot: no se pudo abrir %s (%s)\n",
                LED_STRIP_SPI_DEVICE, strerror(errno));
        return ROBOT_ERR_GPIO;
    }
    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = LED_STRIP_SPI_HZ;
    if (ioctl(g_spi_fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(g_spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(g_spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        close(g_spi_fd);
        g_spi_fd = -1;
        return ROBOT_ERR_GPIO;
    }
#endif

    pthread_mutex_lock(&g_led_lock);
    robot_status_t st = flush_strip();          /* tira apagada */
    g_blink_thread_stop = false;
    pthread_mutex_unlock(&g_led_lock);
    if (st != ROBOT_OK) {
        return st;
    }

    if (pthread_create(&g_blink_thread, NULL, blink_thread_fn, NULL) != 0) {
        return ROBOT_ERR_GPIO;
    }
    g_blink_thread_running = true;
    return ROBOT_OK;
}

void led_module_cleanup(void)
{
    if (g_blink_thread_running) {
        pthread_mutex_lock(&g_led_lock);
        g_blink_thread_stop = true;
        pthread_mutex_unlock(&g_led_lock);
        pthread_join(g_blink_thread, NULL);
        g_blink_thread_running = false;
    }

    pthread_mutex_lock(&g_led_lock);
    memset(g_state, 0, sizeof(g_state));
    flush_strip();                              /* apaga la tira */
    pthread_mutex_unlock(&g_led_lock);

    if (g_spi_fd >= 0) {
        close(g_spi_fd);
        g_spi_fd = -1;
    }
}

/* ========================================================================= */
/* API pública                                                               */
/* ========================================================================= */

static bool valid_led(led_id_t led)
{
    return (int)led >= 0 && (int)led < LED_COUNT_LOGICAL;
}

robot_status_t led_set(led_id_t led, bool on)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;
    if (!valid_led(led))      return ROBOT_ERR_INVALID_PARAM;

    pthread_mutex_lock(&g_led_lock);
    g_state[led].blinking = false;
    g_state[led].on = on;
    robot_status_t st = flush_strip();
    pthread_mutex_unlock(&g_led_lock);
    return st;
}

robot_status_t led_toggle(led_id_t led)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;
    if (!valid_led(led))      return ROBOT_ERR_INVALID_PARAM;

    pthread_mutex_lock(&g_led_lock);
    g_state[led].blinking = false;
    g_state[led].on = !g_state[led].on;
    robot_status_t st = flush_strip();
    pthread_mutex_unlock(&g_led_lock);
    return st;
}

robot_status_t led_blink(led_id_t led, uint32_t freq_hz, uint32_t duration_ms)
{
    if (!g_robot_initialized)       return ROBOT_ERR_NOT_INITIALIZED;
    if (!valid_led(led))            return ROBOT_ERR_INVALID_PARAM;
    if (freq_hz == 0 || freq_hz > 50) return ROBOT_ERR_INVALID_PARAM;

    pthread_mutex_lock(&g_led_lock);
    uint64_t t = now_ms();
    g_state[led].blinking = true;
    g_state[led].freq_hz = freq_hz;
    g_state[led].blink_start_ms = t;
    g_state[led].blink_end_ms = (duration_ms == 0) ? 0 : t + duration_ms;
    g_state[led].on = true;
    robot_status_t st = flush_strip();
    pthread_mutex_unlock(&g_led_lock);
    return st;
}

robot_status_t led_stop_blink(led_id_t led)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;
    if (!valid_led(led))      return ROBOT_ERR_INVALID_PARAM;

    pthread_mutex_lock(&g_led_lock);
    g_state[led].blinking = false;
    g_state[led].on = false;
    robot_status_t st = flush_strip();
    pthread_mutex_unlock(&g_led_lock);
    return st;
}
