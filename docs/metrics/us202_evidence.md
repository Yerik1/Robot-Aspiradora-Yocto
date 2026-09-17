root@raspberrypi4:~# uname -a
Linux raspberrypi4 6.6.63-v7l #1 SMP Fri Dec  6 10:10:05 UTC 2024 armv7l GNU/Linux

root@raspberrypi4:/root# cat /etc/issue
Poky (Yocto Project Reference Distro) 5.0.15 \n \l

lelito@vm:~/poky-scarthgap-5.0.15/rpi4$ time bitbake core-image-minimal 2>&1 | tee build_log.txt
du -sh tmp/deploy/images/raspberrypi4/core-image-minimal-raspberrypi4*.wic.bz2 2>/dev/null || \
du -sh tmp/deploy/images/raspberrypi4/core-image-minimal-raspberrypi4.rpi-sdimg 2>/dev/null
bitbake: command not found

real    0m0.186s
user    0m0.148s
sys    0m0.042s
26M    tmp/deploy/images/raspberrypi4/core-image-minimal-raspberrypi4.rootfs-20260917052430.wic.bz2
4.0K    tmp/deploy/images/raspberrypi4/core-image-minimal-raspberrypi4.rootfs.wic.bz2
