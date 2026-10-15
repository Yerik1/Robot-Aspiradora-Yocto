SUMMARY = "Conexion WiFi del robot: wpa_supplicant + systemd-networkd, IP fija y aviso de IP en pantalla"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://wpa_supplicant-wlan0.conf \
           file://25-wlan.network \
           file://robot-ip.sh \
           file://robot-ip.service \
          "
S = "${WORKDIR}"

inherit systemd

SYSTEMD_SERVICE:${PN} = "robot-ip.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install() {
    install -d ${D}${sysconfdir}/wpa_supplicant
    install -m 0600 ${WORKDIR}/wpa_supplicant-wlan0.conf ${D}${sysconfdir}/wpa_supplicant/
    install -d ${D}${sysconfdir}/systemd/network
    install -m 0644 ${WORKDIR}/25-wlan.network ${D}${sysconfdir}/systemd/network/

    # wpa_supplicant@wlan0 arranca con el sistema y tambien cuando aparece wlan0
    install -d ${D}${sysconfdir}/systemd/system/multi-user.target.wants
    ln -sf ${systemd_system_unitdir}/wpa_supplicant@.service \
        ${D}${sysconfdir}/systemd/system/multi-user.target.wants/wpa_supplicant@wlan0.service
    install -d ${D}${sysconfdir}/systemd/system/sys-subsystem-net-devices-wlan0.device.wants
    ln -sf ${systemd_system_unitdir}/wpa_supplicant@.service \
        ${D}${sysconfdir}/systemd/system/sys-subsystem-net-devices-wlan0.device.wants/wpa_supplicant@wlan0.service

    install -d ${D}${bindir}
    install -m 0755 ${WORKDIR}/robot-ip.sh ${D}${bindir}/
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/robot-ip.service ${D}${systemd_system_unitdir}/
}

FILES:${PN} = "${sysconfdir} ${bindir} ${systemd_system_unitdir}"

# brcmfmac-wcc: modulo de proveedor que brcmfmac necesita en kernel 6.6 para el chip de la Pi 4
RDEPENDS:${PN} = "wpa-supplicant iw kernel-module-brcmfmac kernel-module-brcmfmac-wcc linux-firmware-rpidistro-bcm43455"
RRECOMMENDS:${PN} = "wireless-regdb-static"
