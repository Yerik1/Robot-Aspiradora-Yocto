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
#define PIN_MOTOR_LEFT_IN1   17
#define PIN_MOTOR_LEFT_IN2   27
#define PIN_MOTOR_LEFT_EN    18   /* PWM por hardware (PWM0) */

#define PIN_MOTOR_RIGHT_IN1  22
#define PIN_MOTOR_RIGHT_IN2  23
#define PIN_MOTOR_RIGHT_EN   19   /* PWM por hardware (PWM1) */

#define MOTOR_PWM_FREQ_HZ    1000

/* ==========================================================================
 * Motor de aspirado
 *
 * ASUNCIÓN A VERIFICAR: pin y polaridad de la etapa de switcheo (MOSFET +
 * optoacoplador) que el equipo acordó agregar para este motor. Todavía NO
 * está dibujada en el esquemático KiCad (se ofreció agregarla, pendiente de
 * confirmación). Se deja aquí un pin GPIO libre reservado; ajustar cuando
 * se defina el circuito real. Al no pasar por un L298N no debería tener
 * inversión óptica de un IN de puente H, pero SÍ puede tener inversión si
 * el optoacoplador de la etapa de switcheo está en modo "GPIO alto = corta
 * el motor" -- revisar el circuito final antes de dar por buena la lógica
 * de vacuum_on()/vacuum_off() en motor.c. */
#define PIN_VACUUM_ENABLE    26

/* ==========================================================================
 * Sensores ultrasónicos HC-SR04 (U5, U8, U9)
 *
 * Convención tomada del esquemático: la línea llamada "DATA" de cada
 * sensor es la que dispara el pulso (TRIGGER) y la línea "SCK" es la que
 * se lee (ECHO). U8 fue el único con el detalle adicional de que su línea
 * SCK original estaba fusionada a GND por un error de enrutado (ya
 * corregido en el esquemático, reasignada a un GPIO libre).
 * ========================================================================== */

/* ASUNCIÓN A VERIFICAR: U8 = sensor FRONTAL, U5 = sensor IZQUIERDO,
 * U9 = sensor DERECHO. Depende de en qué posición del chasis se atornille
 * físicamente cada módulo -- son eléctricamente idénticos. */
#define PIN_SENSOR_FRONT_TRIG  16   /* U8 DATA */
#define PIN_SENSOR_FRONT_ECHO   4   /* U8 SCK  */

#define PIN_SENSOR_LEFT_TRIG    6   /* U5 DATA */
#define PIN_SENSOR_LEFT_ECHO    5   /* U5 SCK  */

#define PIN_SENSOR_RIGHT_TRIG  24   /* U9 DATA */
#define PIN_SENSOR_RIGHT_ECHO  25   /* U9 SCK  */

/* Parámetros HC-SR04 (datasheet) */
#define SENSOR_TIMEOUT_US        35000   /* sin eco esperado ~38ms; margen */
#define SENSOR_MIN_GAP_US        60000   /* separación mínima entre disparos
                                            de sensores distintos, para
                                            evitar cross-talk de ecos */
#define SENSOR_SPEED_SOUND_DIV   58.0f   /* cm = pulso_us / 58.0 */

/* ==========================================================================
 * LEDs de estado
 *
 * ASUNCIÓN A VERIFICAR: correspondencia D1-D4 <-> led_id_t. Tomada del
 * orden físico de los LEDs en el esquemático; confirmar contra la
 * serigrafía real de la placa.
 * ========================================================================== */
#define PIN_LED_AUTONOMOUS_MODE  12   /* D1 */
#define PIN_LED_MANUAL_MODE      13   /* D2 */
#define PIN_LED_OBSTACLE_ALERT   20   /* D3 */
#define PIN_LED_SYSTEM_ON        21   /* D4 */

#endif /* ROBOT_PINS_H */
