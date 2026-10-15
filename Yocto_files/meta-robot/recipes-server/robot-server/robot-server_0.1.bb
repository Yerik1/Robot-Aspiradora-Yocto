SUMMARY = "robot-server: Web server and API for Robot Aspiradora"
DESCRIPTION = "Mongoose-based Web Server providing REST/RPC API for robot control"
LICENSE = "CLOSED"

SRC_URI = "git://github.com/Yerik1/Robot-Aspiradora-Yocto.git;protocol=https;branch=feature/integration-webserver-librobot"
SRCREV = "${AUTOREV}"
S = "${WORKDIR}/git/server"

inherit cmake systemd

DEPENDS = "openssl librobot"
RDEPENDS:${PN} = "librobot"

SRC_URI += "file://robot-server.service"

SYSTEMD_SERVICE:${PN} = "robot-server.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install:append() {
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/robot-server.service ${D}${systemd_system_unitdir}
    
    install -d ${D}${localstatedir}/lib/robot-server
}

FILES:${PN} += "${datadir}/robot-server ${systemd_system_unitdir} ${localstatedir}/lib/robot-server"

