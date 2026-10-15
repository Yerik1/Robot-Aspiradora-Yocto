SUMMARY = "Canciones y efectos de sonido del robot"
LICENSE = "CLOSED"

FILESEXTRAPATHS:prepend := "/home/lelito/robot-files:"
SRC_URI = "file://media"
S = "${WORKDIR}/media"

inherit allarch

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    install -d ${D}${datadir}/robot-aspirador/music ${D}${datadir}/robot-aspirador/audio
    install -m 0644 ${S}/musica/*.mp3 ${D}${datadir}/robot-aspirador/music/
    install -m 0644 ${S}/efectos/modoautomatico.mp3 ${D}${datadir}/robot-aspirador/audio/autonomous_start.mp3
    install -m 0644 ${S}/efectos/obstaculo.mp3      ${D}${datadir}/robot-aspirador/audio/obstacle.mp3
    install -m 0644 ${S}/efectos/modomanual.mp3     ${D}${datadir}/robot-aspirador/audio/manual_mode.mp3
}

FILES:${PN} = "${datadir}/robot-aspirador"
