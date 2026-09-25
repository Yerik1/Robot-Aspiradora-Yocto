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
    src/sim_robot.c
    mongoose/mongoose.c
)

add_executable(robot-server ${SERVER_SOURCES})

target_include_directories(robot-server PRIVATE
    src
    mongoose
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(robot-server PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    m
)

install(TARGETS robot-server DESTINATION bin)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../client DESTINATION share/robot-server)
