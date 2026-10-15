/**
 * robot_pins.h
 *
 * Mapeo de pines BCM (numeración GPIO de Broadcom, la que usa pigpio)
 * derivado directamente del esquemático KiCad validado del proyecto
 * ("placa empotrados.kicad_sch", netlist re-exportado y verificado).
 *
 * IMPORTANTE: varias asignaciones lógicas (qué motor es "izquierdo", qué
 * sensor es "frontal", qué LED corresponde a qué modo) NO se pueden derivar
 * del esquemático por sí solas -- dependen de cómo el equipo de hardware
 * monte físicamente cada componente en el chasis. Cada una de esas
 * asignaciones está marcada abajo con "ASUNCIÓN A VERIFICAR". Si al montar
 * el hardware una asignación no coincide (p. ej. el motor izquierdo gira
 * en realidad el lado derecho), basta con intercambiar las constantes aquí
 * -- el resto del firmware no debe tocarse.
 */
#ifndef ROBOT_PINS_H
#define ROBOT_PINS_H

/* ==========================================================================
 * Motores de tracción (L298N, vía optoacopladores PC817)
 * ========================================================================== */

/* ASUNCIÓN A VERIFICAR: Canal A del L298N = MOTOR_LEFT, Canal B = MOTOR_RIGHT.
 * Tomado del esquemático (u1/u2 -> IN1/IN2 canal A; uA1 -> ENA; u3/u4 ->
 * IN3/IN4 canal B; uB1 -> ENB). Si al probar el motor conectado al canal A
 * resulta ser el derecho, intercambiar los dos bloques completos. */
#define PIN_MOTOR_LEFT_IN1   22
#define PIN_MOTOR_LEFT_IN2   23
#define PIN_MOTOR_LEFT_EN    19   /* PWM por hardware (PWM0) */

#define PIN_MOTOR_RIGHT_IN1  17
#define PIN_MOTOR_RIGHT_IN2  27
#define PIN_MOTOR_RIGHT_EN   18   /* PWM por hardware (PWM1) */

/* PWM por SOFTWARE (DMA de pigpio) para los motores. El PWM por hardware
 * comparte el bloque de audio del jack y lo deja mudo
 * ("Cannot flush to my output anymore"). d va de 0 a 1000000. */
#define ROBOT_SOFT_PWM(g, f, d) \
    (gpioSetMode((g), PI_OUTPUT), gpioSetPWMrange((g), 1000), \
     gpioSetPWMfrequency((g), (f)), gpioPWM((g), (unsigned)(d) / 1000U))

/* DEMO1_MOTORES_INTERCAMBIADOS: canal izquierdo y derecho intercambiados en hardware */
#define MOTOR_PWM_FREQ_HZ    1000

/* ==========================================================================
 * Motor de aspirado (MOSFET canal N de lado bajo, ver ADR-002)
 * ========================================================================== */
#define PIN_VACUUM_ENABLE    26
#define VACUUM_ON_LEVEL      0   /* circuito invertido: LOW = motor rapido, HIGH = casi detenido */
/* DEMO1_ASPIRADORA: nivel invertido respecto a la version anterior */
#define VACUUM_LEVEL(on)     ((on) ? VACUUM_ON_LEVEL : !VACUUM_ON_LEVEL)

/* ==========================================================================
 * Sensor frontal: ultrasónico HC-SR04 (U8)
 *
 * La línea "DATA" del esquemático dispara el pulso (TRIGGER) y la línea
 * "SCK" se lee (ECHO). ECHO entra al GPIO a través de un divisor de tensión
 * (5 V -> 3.3 V).
 * ========================================================================== */
#define PIN_SENSOR_FRONT_TRIG  16   /* U8 DATA */
#define PIN_SENSOR_FRONT_ECHO   4   /* U8 SCK  */

/* Parámetros HC-SR04 (datasheet) */
#define SENSOR_TIMEOUT_US        35000   /* sin eco esperado ~38ms; margen */
#define SENSOR_MIN_GAP_US        60000   /* separación mínima entre disparos
                                            consecutivos del HC-SR04, para que
                                            se disipe el eco anterior */
#define SENSOR_SPEED_SOUND_DIV   58.0f   /* cm = pulso_us / 58.0 */

/* ==========================================================================
 * Sensores laterales: ópticos reflectivos TCRT5000L
 *
 * Fototransistor con pull-up externo de 10 kOhm a 3.3 V (no se usa el
 * pull interno). LED emisor alimentado desde 3.3 V con 220 Ohm.
 * Salida en BAJO = hay reflejo (obstáculo); ALTO = libre.
 * Alcance útil según hoja de datos: ~0.2 a 15 mm.
 *
 * ASUNCIÓN A VERIFICAR: GPIO de cada sensor. Se reutilizan las líneas que
 * antes eran ECHO de los HC-SR04 laterales (U5 y U9); cambiar aquí si en
 * el esquemático quedaron en otros pines. Los GPIO 6 y 24 (antes TRIG de
 * esos sensores) quedan libres.
 * ========================================================================== */
#define PIN_SENSOR_LEFT_IR     5
#define PIN_SENSOR_RIGHT_IR   25

#define IR_ACTIVE_LEVEL        0     /* nivel leído cuando hay obstáculo */
#define IR_SAMPLES             5     /* lecturas por consulta (votación) */
#define IR_SAMPLE_GAP_US     200     /* separación entre lecturas */

/* ==========================================================================
 * LEDs de estado: tira de 4 LEDs SK6812MINI-E (RGB, orden GRB), uno por indicador
 *
 * Se controla por SPI0 (/dev/spidev0.0). Solo se usa MOSI = GPIO 10
 * (pin físico 19) como línea de datos; SCLK y CE quedan sin conectar.
 * Cada bit del LED se codifica en 4 bits de SPI a 3.2 MHz
 * (0 -> 1000, 1 -> 1100), lo que da 0.31 us / 0.62 us en alto y
 * 1.25 us por bit, dentro de la temporización del SK6812.
 *
 * Requiere en la imagen: dtparam=spi=on, core_freq=500 y core_freq_min=500
 * (la frecuencia del reloj SPI depende del núcleo) y los módulos
 * spi-bcm2835 y spidev.
 *
 * Los GPIO 12, 13, 20 y 21 (antes LEDs discretos D1-D4) quedan libres.
 * ========================================================================== */
#define LED_STRIP_SPI_DEVICE   "/dev/spidev0.0"
#define LED_STRIP_SPI_HZ       3200000
#define LED_STRIP_COUNT        4
#define LED_STRIP_MAX_LEVEL    40     /* brillo máximo por canal (0-255),
                                         limita el consumo de la tira */

/* ASUNCIÓN A VERIFICAR: qué tramo de la tira representa cada indicador.
 * Depende de cómo se monte la tira en el chasis. Cada tramo es
 * [inicio, inicio + cantidad). */
#define LED_ZONE_SYSTEM_ON_START        0
#define LED_ZONE_SYSTEM_ON_COUNT       1
#define LED_ZONE_AUTONOMOUS_START      1
#define LED_ZONE_AUTONOMOUS_COUNT      1
#define LED_ZONE_MANUAL_START          2
#define LED_ZONE_MANUAL_COUNT          1
#define LED_ZONE_OBSTACLE_START        3
#define LED_ZONE_OBSTACLE_COUNT        1

#endif /* ROBOT_PINS_H */
