SUMMARY = "Metricas de eficiencia de recursos del robot: rootfs, arranque, RAM y CPU (Demo 1)"
LICENSE = "CLOSED"

SRC_URI = "file://robot-metricas.sh \
           file://robot-metricas-arranque.service \
           file://robot-demo1-banner.sh \
          "
S = "${WORKDIR}"

inherit systemd allarch

SYSTEMD_SERVICE:${PN} = "robot-metricas-arranque.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${WORKDIR}/robot-metricas.sh ${D}${bindir}/robot-metricas
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/robot-metricas-arranque.service ${D}${systemd_system_unitdir}/
    install -d ${D}${sysconfdir}/profile.d
    install -m 0644 ${WORKDIR}/robot-demo1-banner.sh ${D}${sysconfdir}/profile.d/
}

FILES:${PN} += "${sysconfdir}/profile.d"
RDEPENDS:${PN} = "systemd-analyze"
