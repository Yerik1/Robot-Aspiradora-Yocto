root@raspberrypi4:~# uname -a
Linux raspberrypi4 6.6.63-v7l #1 SMP Fri Dec  6 10:10:05 UTC 2024 armv7l GNU/Linux

root@raspberrypi4:/root# cat /etc/issue
Poky (Yocto Project Reference Distro) 5.0.15 \n \l

lelito@vm:~/poky-scarthgap-5.0.15/rpi4$ bitbake -c cleansstate core-image-minimal
lelito@vm:~/poky-scarthgap-5.0.15/rpi4$ time bitbake core-image-minimal

Sstate summary: Wanted 103 Local 98 Mirrors 0 Missed 5 Current 1723 (95% match, 99% complete)
NOTE: Tasks Summary: Attempted 4059 tasks of which 4039 didn't need to be rerun and all succeeded.

real    1m6.436s
user    0m1.073s
sys     0m0.285s
