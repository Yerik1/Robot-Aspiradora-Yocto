#!/bin/sh
# Muestra en el monitor la IP del WiFi cuando la consigue.
IP=""
for i in $(seq 1 60); do
    IP=$(ip -4 addr show wlan0 2>/dev/null | awk '/inet /{print $2}' | tr '\n' ' ')
    case "$IP" in *172.20.10.*) break ;; esac
    sleep 2
done
sleep 5
IP=$(ip -4 addr show wlan0 2>/dev/null | awk '/inet /{print $2}' | tr '\n' ' ')
echo "=================================================="
echo " WIFI wlan0: ${IP:-SIN IP (revisar hotspot del iPhone)}"
echo " SERVIDOR:   http://172.20.10.14:8080"
echo "=================================================="
