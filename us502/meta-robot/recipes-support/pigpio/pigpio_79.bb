SUMMARY = "pigpio: biblioteca C para control de GPIO/PWM en Raspberry Pi"
HOMEPAGE = "https://github.com/joan2937/pigpio"
LICENSE = "Unlicense"
LIC_FILES_CHKSUM = "file://UNLICENCE;md5=61287f92700ec1bdf13bc86d8228cd13"

# v79 (ultima version publicada)
SRC_URI = "git://github.com/joan2937/pigpio.git;protocol=https;branch=master"
SRCREV = "c33738a320a3e28824af7807edafda440952c05d"
S = "${WORKDIR}/git"

inherit cmake

# pigpio solo funciona en Raspberry Pi
COMPATIBLE_MACHINE = "^rpi$"

# El CMakeLists de pigpio intenta instalar el modulo Python con el python del host
EXTRA_OECMAKE += "-DCMAKE_DISABLE_FIND_PACKAGE_Python=ON"

# pigpio genera las .so sin version: van al paquete principal, no al -dev
FILES_SOLIBSDEV = ""
FILES:${PN} += "${libdir}/*.so"
INSANE_SKIP:${PN} += "dev-so"

# pigpio instala los man en /usr/man
FILES:${PN}-doc += "${prefix}/man"
RDEPENDS:${PN} += "libgcc"
