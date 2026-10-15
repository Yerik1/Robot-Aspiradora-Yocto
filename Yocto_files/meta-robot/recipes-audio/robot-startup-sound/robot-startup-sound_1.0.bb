SUMMARY = "Audio del robot: sonido de inicio y salida por jack 3.5mm"
LICENSE = "CLOSED"

SRC_URI = "file://system_start.mp3 \
           file://robot-startup-sound.service \
           file://99-robot-audio.conf \
           file://snd-bcm2835.conf \
           file://snd-bcm2835-options.conf"
S = "${WORKDIR}"

inherit systemd
SYSTEMD_SERVICE:${PN} = "robot-startup-sound.service"
SYSTEMD_AUTO_ENABLE = "enable"

RDEPENDS:${PN} = "mpg123 kernel-module-snd-bcm2835"

do_install() {
    install -d ${D}${datadir}/robot-aspirador/audio
    install -m 0644 ${WORKDIR}/system_start.mp3 ${D}${datadir}/robot-aspirador/audio/
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/robot-startup-sound.service ${D}${systemd_system_unitdir}/
    install -d ${D}${sysconfdir}/alsa/conf.d
    install -m 0644 ${WORKDIR}/99-robot-audio.conf ${D}${sysconfdir}/alsa/conf.d/
    install -d ${D}${sysconfdir}/modules-load.d
    install -m 0644 ${WORKDIR}/snd-bcm2835.conf ${D}${sysconfdir}/modules-load.d/
    install -d ${D}${sysconfdir}/modprobe.d
    install -m 0644 ${WORKDIR}/snd-bcm2835-options.conf ${D}${sysconfdir}/modprobe.d/snd-bcm2835.conf
}

FILES:${PN} += "${datadir}/robot-aspirador/audio"
