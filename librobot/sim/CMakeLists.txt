# sim/CMakeLists.txt
#
# Proyecto de SIMULACION para desarrollo local, separado del CMakeLists.txt
# de la raiz (ese sigue exigiendo la libpigpio real para el build de
# produccion en la Raspberry Pi/Yocto -- no se toca aqui).
#
# Compila el codigo de produccion SIN MODIFICAR (../src/*.c) contra
# pigpio_sim.c (ver ese archivo) en vez de la libpigpio real, para poder
# ejercitar motor.c/sensors.c/leds.c y tests/test_librobot.c en un host
# x86/WSL sin la Raspberry Pi ni el hardware conectado.
#
# Uso:
#   cmake -S sim -B sim/build
#   cmake --build sim/build
#   ./sim/build/test_librobot_sim
#
# Esto valida la LOGICA de software (matematica de compensacion PWM,
# rampas, maquina de estados de LEDs, timing trigger/echo de sensores,
# validacion de parametros). NO reemplaza la validacion en la Raspberry Pi
# real con motores/sensores/LEDs fisicos conectados.

cmake_minimum_required(VERSION 3.10)
project(librobot_sim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra)

add_library(robot_sim STATIC
    pigpio_sim.c
    ../src/librobot.c
    ../src/motor.c
    ../src/sensors.c
    ../src/leds.c
    ../src/audio.c
)

# Unico proposito: que "#include <pigpio.h>" en los .c de produccion
# resuelva a nuestro pigpio.h simulado en vez de buscar la libpigpio real.
target_include_directories(robot_sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(robot_sim PUBLIC pthread)

add_executable(test_librobot_sim ../tests/test_librobot.c)
target_link_libraries(test_librobot_sim PRIVATE robot_sim)

# Habilita en test_librobot.c las aserciones automaticas basadas en
# sim/sim_probe.h (introspeccion del hardware simulado), en vez del flujo
# de confirmacion manual pensado para la Raspberry Pi real.
target_compile_definitions(test_librobot_sim PRIVATE LIBROBOT_SIM_PROBES=1)
