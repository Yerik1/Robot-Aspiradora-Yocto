SUMMARY = "librobot: API de hardware del Robot Aspiradora"
LICENSE = "CLOSED"

# Temporal: fuentes locales en la VM. Al subir al repo se cambia a git.
FILESEXTRAPATHS:prepend := "/home/lelito/robot-files:"
SRC_URI = "file://librobot"
S = "${WORKDIR}/librobot"

inherit cmake

# pigpio: build y runtime. mpg123: proceso externo (solo runtime).
DEPENDS = "pigpio"
RDEPENDS:${PN} = "pigpio mpg123"

# Binario de prueba en paquete aparte
PACKAGES =+ "${PN}-selftest"
FILES:${PN}-selftest = "${bindir}/robot-selftest"
RDEPENDS:${PN} += "kernel-module-spi-bcm2835 kernel-module-spidev"
