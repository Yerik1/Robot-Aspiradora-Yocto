SUMMARY = "Robot Aspiradora Autonomo - Demo 1 (CE-1113): librobot, servidor web, WiFi, audio y metricas"
require recipes-core/images/core-image-minimal.bb

IMAGE_INSTALL:append = " librobot librobot-selftest robot-startup-sound alsa-utils-amixer \
                         robot-server robot-wifi robot-media robot-metricas"
IMAGE_INSTALL:remove = "robot-demo robot-demo-aventura robot-solo-audio"
