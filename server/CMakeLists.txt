cmake_minimum_required(VERSION 3.10)
project(robot-server C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_definitions(-D_GNU_SOURCE)
add_compile_options(-Wall -Wextra)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

set(SERVER_SOURCES
    src/main.c
    src/rpc_handlers.c
    src/auth.c
    src/crypto_util.c
    src/hw_robot.c
    mongoose/mongoose.c
)

add_executable(robot-server ${SERVER_SOURCES})

target_include_directories(robot-server PRIVATE
    src
    mongoose
    ${OPENSSL_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../librobot/include
)

target_link_libraries(robot-server PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    m
    robot
)

install(TARGETS robot-server DESTINATION bin)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../client DESTINATION share/robot-server)

# Audio: se instala en las mismas carpetas que usa librobot (audio.c).
#   music/*.mp3   -> share/robot-aspirador/music  (canciones del reproductor)
#   SoundEffects/ -> share/robot-aspirador/audio  (notificaciones), con el
#                    nombre que espera librobot. inicio.mp3 no se instala:
#                    system_start.mp3 ya lo instala la receta
#                    robot-startup-sound.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/music)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/music/
            DESTINATION share/robot-aspirador/music
            FILES_MATCHING PATTERN "*.mp3")
endif()

set(SFX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/SoundEffects)
foreach(pair "modoautomatico.mp3:autonomous_start.mp3"
             "obstaculo.mp3:obstacle.mp3"
             "modomanual.mp3:manual_mode.mp3")
    string(REPLACE ":" ";" pair_list ${pair})
    list(GET pair_list 0 sfx_src)
    list(GET pair_list 1 sfx_dst)
    if(EXISTS ${SFX_DIR}/${sfx_src})
        install(FILES ${SFX_DIR}/${sfx_src}
                DESTINATION share/robot-aspirador/audio
                RENAME ${sfx_dst})
    else()
        message(WARNING "No se encontro SoundEffects/${sfx_src}")
    endif()
endforeach()
