# CMakeLists.txt
#
# Scaffold mínimo para compilar librobot.so localmente durante desarrollo
# (fuera del entorno Yocto). El empaquetado final para el rootfs de Yocto
# (recipe .bb, US-304) es un paso aparte, no cubierto por este archivo.
#
# Requiere que la librería de desarrollo de pigpio (libpigpio.so + pigpio.h)
# esté instalada en el sistema donde se compile. En Raspberry Pi OS suele
# venir preinstalada; en otros sistemas: apt install libpigpio-dev, o
# compilar pigpio desde https://github.com/joan2937/pigpio.

cmake_minimum_required(VERSION 3.10)
project(librobot C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra)

add_library(robot SHARED
    src/librobot.c
    src/motor.c
    src/sensors.c
    src/leds.c
    src/audio.c
)

target_include_directories(robot PUBLIC include)

find_library(PIGPIO_LIB pigpio REQUIRED)
target_link_libraries(robot PUBLIC ${PIGPIO_LIB} pthread)

set_target_properties(robot PROPERTIES
    PUBLIC_HEADER include/librobot.h
    OUTPUT_NAME robot
)

# audio.c invoca "mpg123" como proceso externo (fork+exec), no lo enlaza
# como biblioteca -- por eso no hace falta find_library ni target_link_libraries
# para mpg123 aquí. Sí tiene que quedar declarado como RDEPENDS en la receta
# de Yocto de librobot (US-502), como dependencia de ejecución, no de build.

# --- Instalación (US-304: "Cabecera librobot.h instalada junto con la
# biblioteca") ---
install(TARGETS robot
    LIBRARY DESTINATION lib
    PUBLIC_HEADER DESTINATION include
)

# --- CPack (US-304: "CPack configurado para generar el paquete de
# código fuente") ---
set(CPACK_PACKAGE_NAME "librobot")
set(CPACK_PACKAGE_VERSION "0.1.0")
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_IGNORE_FILES
    "/build/"
    "/\\\\.git/"
    "\\\\.gitignore"
)
include(CPack)
