/**
 * pigpio.h (SIMULADO)
 *
 * Sustituto de la libpigpio real, SOLO para compilar y ejecutar el codigo
 * fuente de src/ en un host de desarrollo (x86/WSL) sin la Raspberry Pi ni
 * el hardware conectado. Declara exactamente el subconjunto de la API real
 * de pigpio que motor.c/sensors.c/leds.c/librobot.c usan (ver
 * pigpio_sim.c para la implementacion simulada).
 *
 * NO se usa para el build de produccion: el CMakeLists.txt de la raiz del
 * repo sigue exigiendo la libpigpio real via find_library(). Este header
 * solo se ve cuando se compila explicitamente el proyecto de sim/.
 */
#ifndef PIGPIO_SIM_H
#define PIGPIO_SIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PI_INPUT  0
#define PI_OUTPUT 1

int gpioInitialise(void);
void gpioTerminate(void);

int gpioSetMode(unsigned gpio, unsigned mode);
int gpioWrite(unsigned gpio, unsigned level);
int gpioRead(unsigned gpio);

/* PWMduty en pigpio real: 0-1000000 (fraccion de 1e6, no 0-100). */
int gpioHardwarePWM(unsigned gpio, unsigned PWMfreq, unsigned PWMduty);

int gpioTrigger(unsigned user_gpio, unsigned pulseLen, unsigned level);

uint32_t gpioTick(void);
uint32_t gpioDelay(uint32_t micros);

#ifdef __cplusplus
}
#endif

#endif /* PIGPIO_SIM_H */
