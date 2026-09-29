SUMMARY = "librobot: API de hardware del Robot Aspiradora"
LICENSE = "CLOSED"

SRC_URI = "git://github.com/Yerik1/Robot-Aspiradora-Yocto.git;protocol=https;branch=develop"
SRCREV = "${AUTOREV}"
S = "${WORKDIR}/git/librobot"

inherit cmake

# pigpio: build y runtime. mpg123: proceso externo (solo runtime).
DEPENDS = "pigpio"
RDEPENDS:${PN} = "pigpio mpg123"

# Binario de prueba en paquete aparte
PACKAGES =+ "${PN}-selftest"
FILES:${PN}-selftest = "${bindir}/robot-selftest"
