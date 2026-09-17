# Aspiradora-Yocto

Primer Proyecto del curso de Sistemas Empotrados II-Semestre 2026 Tecnologico de Costa Rica

## Configuración de Yocto

- MACHINE: `raspberrypi4`
- El toolchain generado es de 64 bits (el compilador reportado por
  `environment-setup-cortexa72-poky-linux` es `aarch64-poky-linux-gcc`),
  así que aunque no se usó `raspberrypi4-64` explícitamente, la imagen
  resultante es de arquitectura aarch64.