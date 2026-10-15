SUMMARY = "Servidor web del robot aspiradora (Mongoose, JSON-RPC sobre WebSocket)"
LICENSE = "CLOSED"

FILESEXTRAPATHS:prepend := "/home/lelito/robot-files:"
SRC_URI = "file://server \
           file://client \
           file://robot-server.service \
          "
S = "${WORKDIR}/server"

inherit cmake systemd

DEPENDS = "librobot openssl"
RDEPENDS:${PN} += "robot-media"

SYSTEMD_SERVICE:${PN} = "robot-server.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install:append() {
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/robot-server.service ${D}${systemd_system_unitdir}/
    # El audio lo instala robot-media
    rm -rf ${D}${datadir}/robot-aspirador
}

FILES:${PN} += "${datadir}/robot-server"
