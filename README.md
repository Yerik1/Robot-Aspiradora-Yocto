# Robot Aspiradora Autónomo — CE-1113

Proyecto del curso Sistemas Empotrados (CE-1113), Tecnológico de Costa Rica. Robot aspirador autónomo sobre un chasis de aspiradora robótica reutilizado, controlado por una Raspberry Pi 4 corriendo una imagen Linux embebida basada en Yocto (Poky).

> Entrega: 2026-10-06

## Mapeo de pines GPIO (BCM)

Derivado del esquemático KiCad validado (`placa empotrados.kicad_sch`). Definido en `librobot/src/robot_pins.h`.

| Función | Pin(es) BCM | Nota |
|---|---|---|
| Motor izquierdo — IN1/IN2 | 17 / 27 | Canal A del L298N. |
| Motor izquierdo — EN (PWM) | 18 | PWM0 por hardware |
| Motor derecho — IN1/IN2 | 22 / 23 | Canal B del L298N |
| Motor derecho — EN (PWM) | 19 | PWM1 por hardware |
| Motor de aspirado — enable | 26 | Pin reservado; ajustar cuando se defina el circuito real de switcheo |
| Sensor frontal — TRIG/ECHO | 16 / 4 | U8. |
| Sensor izquierdo — TRIG/ECHO | 6 / 5 | U5 |
| Sensor derecho — TRIG/ECHO | 24 / 25 | U9 |
| LED modo autónomo | 12 | D1. |
| LED modo manual | 13 | D2 |
| LED alerta de obstáculo | 20 | D3 |
| LED sistema encendido | 21 | D4 |

## `librobot.so`

Única biblioteca con acceso a hardware (GPIO/PWM/audio). El servidor web nunca accede a GPIO directamente (US-402).

- **Inicialización**: `robot_init()` / `robot_cleanup()` orquestan `gpioInitialise()`/`gpioTerminate()` y el init/cleanup de cada módulo (motor, sensores, LEDs, audio) en orden. Un flag `g_robot_initialized` gatea todas las funciones públicas.
- **Tracción**: `motor_set_speed()`, `motor_stop()`, `motor_stop_all()`, más movimientos compuestos (`robot_move_forward/backward`, `robot_turn_left/right`).
- **Aspirado**: `vacuum_on()` / `vacuum_off()` / `vacuum_is_on()` — actuador on/off, sin control de velocidad ni dirección.
- **Sensores**: `sensor_read_distance_cm()`, `sensor_obstacle_detected()`.
- **LEDs**: `led_set()`, `led_toggle()`, `led_blink()` (no bloqueante), `led_stop_blink()`.
- **Audio**: `audio_play_file()`, `audio_play_notification()` (4 eventos obligatorios: arranque del sistema, inicio de modo autónomo, obstáculo, modo manual), `audio_pause/resume/stop`, `audio_set_volume/get_volume`, `audio_get_state`, `audio_list_files`. Implementado sobre un proceso `mpg123 -R` persistente controlado por pipe.

Build local (fuera de Yocto, para desarrollo/pruebas):

```
mkdir -p build && cd build
cmake ..
make
```

Requiere `libpigpio-dev` instalado en el sistema de build. Genera `librobot.so` + instala `librobot.h` (`make install`). `mpg123` es una dependencia de **ejecución** (proceso externo vía fork+exec), no de enlazado — debe quedar como `RDEPENDS` en la receta Yocto de `librobot` (US-502).

## Entorno de build (Yocto)

- Distro base: Poky `scarthgap` (5.0.15)
- `MACHINE`: `raspberrypi4`
- Arquitectura: ARM 32-bit (`armv7l`) — confirmado en hardware real vía `uname -a`
- Layers (`Yocto_files/Local_config/bblayers.conf`): `meta`, `meta-poky`, `meta-yocto-bsp`, `meta-raspberrypi`
- Configuración custom en `local.conf` (`Yocto_files/Local_config/local.conf`):
  - `INHERIT += "rm_work"` — borra los directorios de trabajo de cada receta tras compilarla, para ahorrar espacio en disco.
  - `DL_DIR` fijado a una ruta compartida de descargas, reutilizable entre builds.
  - `LICENSE_FLAGS_ACCEPTED += "synaptics-killswitch"` — [completar: qué paquete/receta lo requirió y por qué]
  - `IMAGE_FEATURES += "ssh-server-dropbear"` — agrega servidor SSH (dropbear) a la imagen para acceso remoto durante desarrollo y pruebas.
- Capa propia `meta-robot` (esqueleto inicial): `recipes-audio/`, `recipes-bsp/`, `recipes-connectivity/`, `recipes-image/`, `recipes-kernel/`, `recipes-multimedia/`, `recipes-robot/` (robot-server), `recipes-support/`.

Build:

```
source poky-scarthgap-5.0.15/oe-init-build-env rpi4
bitbake core-image-minimal
```

(`oe-init-build-env` debe re-ejecutarse en cada sesión de shell nueva — el `PATH` que agrega no persiste entre sesiones.)

## Verificación de dependencias del host (US-201)

Script de verificación corrido en al menos 2 equipos de desarrollo, todos los paquetes requeridos por Yocto confirmados como instalados. Evidencia: `docs/metrics/host-check1.md`, `docs/metrics/host-check2.md`.

## Evidencia de build y arranque en hardware real (US-202)

- Imagen generada: `core-image-minimal-raspberrypi4.rootfs-*.wic.bz2`, tamaño ~26M.
- Arranque confirmado en Raspberry Pi 4 física:
  - `uname -a` → `Linux raspberrypi4 6.6.63-v7l ... armv7l GNU/Linux`
  - `/etc/issue` → `Poky (Yocto Project Reference Distro) 5.0.15`
- Tiempo de build incremental (tras `bitbake -c cleansstate core-image-minimal`, 95% del sstate del resto de recetas reutilizado): `1m6.436s`. No es un tiempo de build desde cero.
- Detalle completo (comandos y salidas): `docs/metrics/us202_evidence.md`.
