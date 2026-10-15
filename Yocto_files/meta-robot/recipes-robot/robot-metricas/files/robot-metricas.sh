#!/bin/sh
# robot-metricas - Metricas de eficiencia de recursos (Robot Aspiradora, Demo 1)
#   robot-metricas            reporte completo; CPU promediado en 10 s
#   robot-metricas 30         igual, promediando la CPU en 30 s
#   robot-metricas --arranque solo rootfs y tiempo de arranque (lo usa el servicio de boot)
# Cada reporte se agrega a /var/log/robot-metricas.txt
# Correr el reporte completo en operacion normal: modo autonomo + musica + pagina web abierta.

LOG=/var/log/robot-metricas.txt
MODE=full
SEG=10
case "$1" in
    --arranque) MODE=boot ;;
    ''|*[!0-9]*) ;;
    *) SEG=$1 ;;
esac

pid_of() { pidof "$1" 2>/dev/null | awk '{print $1}'; }
ticks_of() { if [ -n "$1" ] && [ -r "/proc/$1/stat" ]; then awk '{print $14 + $15}' "/proc/$1/stat"; else echo 0; fi; }
rss_of() { if [ -n "$1" ] && [ -r "/proc/$1/status" ]; then awk '/^VmRSS/ {printf "%.1f MB", $2 / 1024}' "/proc/$1/status"; else echo "no esta corriendo"; fi; }
cpu_now() { awk '/^cpu / {print $2+$3+$4+$5+$6+$7+$8+$9, $5+$6}' /proc/stat; }

{
echo "=================================================================="
echo " METRICAS ROBOT ASPIRADORA - DEMO 1"
echo " Fecha: $(date '+%Y-%m-%d %H:%M:%S')   Uptime: $(cut -d' ' -f1 /proc/uptime) s"
echo "=================================================================="

echo "--- 1. Tamano del sistema de archivos raiz (referencia <= 200 MB) ---"
echo " Contenido del rootfs (du -sx /): $(du -sxm / 2>/dev/null | awk '{print $1}') MB"
df -m / | awk 'NR == 2 {print " Particion raiz (df -m /): usados " $3 " MB de " $2 " MB"}'

echo "--- 2. Tiempo de arranque (referencia <= 15 s) ---"
T=$(systemctl show -p ActiveEnterTimestampMonotonic --value robot-server.service 2>/dev/null)
if [ -n "$T" ] && [ "$T" -gt 0 ] 2>/dev/null; then
    awk -v t="$T" 'BEGIN {printf " robot-server operativo a los %.2f s del inicio del kernel\n", t / 1000000}'
else
    echo " robot-server todavia no esta activo"
fi
if systemd-analyze >/dev/null 2>&1; then
    systemd-analyze | head -1 | sed 's/^/ systemd-analyze: /'
    echo " Servicios mas lentos (systemd-analyze blame):"
    systemd-analyze blame 2>/dev/null | head -5 | sed 's/^/   /'
else
    echo " systemd-analyze: el arranque aun no termina (volver a correr en unos segundos)"
fi
echo " Nota: no incluye el firmware/bootloader de la Pi (antes del kernel)."

if [ "$MODE" = full ]; then
    echo "--- 3. Memoria RAM ---"
    awk '/^MemTotal/ {t = $2} /^MemAvailable/ {a = $2}
         END {printf " Total %.0f MB | En uso %.0f MB (%.1f %%) | Disponible %.0f MB\n", t/1024, (t-a)/1024, 100*(t-a)/t, a/1024}' /proc/meminfo
    P_SRV=$(pid_of robot-server)
    P_MP3=$(pid_of mpg123)
    echo " robot-server: $(rss_of "$P_SRV")"
    echo " mpg123 (audio): $(rss_of "$P_MP3")"

    echo "--- 4. CPU (promedio de $SEG s, 4 nucleos) ---"
    set -- $(cpu_now); T1=$1; I1=$2
    S1=$(ticks_of "$P_SRV"); M1=$(ticks_of "$P_MP3")
    sleep "$SEG"
    set -- $(cpu_now); T2=$1; I2=$2
    S2=$(ticks_of "$P_SRV"); M2=$(ticks_of "$P_MP3")
    awk -v t="$((T2 - T1))" -v i="$((I2 - I1))" 'BEGIN {if (t > 0) printf " CPU total del sistema: %.1f %%\n", 100 * (t - i) / t}'
    awk -v d="$((S2 - S1))" -v s="$SEG" 'BEGIN {printf " robot-server: %.1f %% de un nucleo\n", d / s}'
    awk -v d="$((M2 - M1))" -v s="$SEG" 'BEGIN {printf " mpg123 (audio): %.1f %% de un nucleo\n", d / s}'
    echo " Carga promedio (1/5/15 min): $(cut -d' ' -f1-3 /proc/loadavg)"
    if [ -r /sys/class/thermal/thermal_zone0/temp ]; then
        awk '{printf " Temperatura del SoC: %.1f C\n", $1 / 1000}' /sys/class/thermal/thermal_zone0/temp
    fi
    echo "--- 5. Estado de los servicios ---"
    for s in robot-server wpa_supplicant@wlan0 robot-startup-sound; do
        echo " $s: $(systemctl is-active $s 2>/dev/null)"
    done
fi
echo "=================================================================="
} | tee -a "$LOG"
