/**
 * audio.c
 *
 * Implementación de US-303: reproducción de audio MP3 mediante `mpg123`
 * como proceso hijo no bloqueante, controlado por su modo de control
 * remoto (`mpg123 -R`).
 *
 * Diseño: al inicializar el módulo se lanza UN solo proceso `mpg123 -R`
 * que queda vivo mientras dure el programa, escuchando comandos de texto
 * por su entrada estándar (conectada aquí a un pipe). Cada función
 * pública de este archivo simplemente escribe una línea de comando en
 * ese pipe (operación no bloqueante y prácticamente instantánea) en vez
 * de lanzar un proceso nuevo por cada reproducción -- así la música de
 * fondo o una notificación pueden sonar sin bloquear el hilo que llama
 * a la biblioteca, incluyendo el hilo principal del algoritmo de
 * navegación.
 *
 * El estado (`audio_state_t`, volumen, archivo actual) se lleva de forma
 * local en este módulo a partir de los comandos que NOSOTROS enviamos,
 * sin parsear las respuestas que `mpg123 -R` imprime por su salida
 * estándar (se descartan a /dev/null). Es una simplificación deliberada:
 * evita implementar un parser del protocolo de texto de mpg123, a costa
 * de no detectar por sí solo si la reproducción terminó sola (fin de
 * pista) o si el proceso murió a medio reproducir. Si eso resulta
 * necesario más adelante, la mejora natural es leer la salida del pipe
 * en un hilo aparte y parsear las líneas "@P <código>".
 *
 * Trade-off de robustez aceptado: si `mpg123` se cae o no está instalado,
 * `audio_module_init()` lo reporta (ROBOT_ERR_AUDIO) o, si muere después
 * de iniciado, las siguientes escrituras al pipe fallan silenciosamente
 * (se ignora SIGPIPE a propósito para no matar todo el proceso del
 * robot) y las funciones devuelven ROBOT_ERR_AUDIO -- no hay
 * auto-recuperación/relanzamiento automático del proceso mpg123.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "../include/librobot.h"
#include "internal.h"

/* ASUNCIÓN A VERIFICAR: rutas reales donde el equipo va a colocar los
 * archivos de audio dentro del rootfs. Se separan dos directorios:
 * uno fijo para los 4 sonidos de notificación obligatorios, y otro para
 * la lista de reproducción de música de fondo que expone
 * audio_list_files(). Ajustar aquí si la convención final es otra. */
#define AUDIO_NOTIFICATION_DIR "/usr/share/robot-server/server/SoundEffects"
#define AUDIO_MUSIC_DIR        "/usr/share/robot-server/server/music"

/* Nombres reales de los archivos en server/SoundEffects/ (se instalan en
 * AUDIO_NOTIFICATION_DIR desde la receta de Yocto). */
static const char *g_notification_files[] = {
    [AUDIO_EVENT_SYSTEM_START]     = AUDIO_NOTIFICATION_DIR "/inicio.mp3",
    [AUDIO_EVENT_AUTONOMOUS_START] = AUDIO_NOTIFICATION_DIR "/modoautomatico.mp3",
    [AUDIO_EVENT_OBSTACLE]         = AUDIO_NOTIFICATION_DIR "/obstaculo.mp3",
    [AUDIO_EVENT_MANUAL_MODE]      = AUDIO_NOTIFICATION_DIR "/modomanual.mp3",
};

/* Canal de notificaciones (ver audio_play_notification()):
 * la notificación suena en un proceso `mpg123 -q` propio mientras la
 * música (proceso `mpg123 -R` persistente) se atenúa al
 * AUDIO_DUCK_PERCENT de su volumen; al terminar se restaura. */
#define AUDIO_DUCK_PERCENT      25    /* % del volumen del usuario durante la notificación */
#define AUDIO_NOTIF_COOLDOWN_S  1.0   /* mínimo entre dos notificaciones del mismo evento */

static pthread_mutex_t g_audio_lock = PTHREAD_MUTEX_INITIALIZER;
static pid_t g_mpg123_pid = -1;
static int g_mpg123_stdin_fd = -1;
static audio_state_t g_audio_state = AUDIO_STOPPED;
static uint8_t g_audio_volume = 80; /* valor inicial razonable; ASUNCIÓN A VERIFICAR */
static char g_current_file[256] = {0};

static pid_t g_notif_pid = -1;        /* proceso de la notificación en curso */
static bool g_music_ducked = false;   /* música atenuada por una notificación */
static double g_last_notif_time[AUDIO_EVENT_MANUAL_MODE + 1];

/** Escribe una línea de comando en el pipe hacia mpg123 -R. `cmd` debe
 *  terminar en '\n'. No bloquea salvo que el pipe esté lleno (buffer del
 *  kernel), lo cual no ocurre en la práctica para líneas de comando cortas. */
static robot_status_t send_command(const char *cmd)
{
    if (g_mpg123_stdin_fd < 0) {
        return ROBOT_ERR_AUDIO;
    }
    size_t len = strlen(cmd);
    ssize_t written = write(g_mpg123_stdin_fd, cmd, len);
    if (written < 0 || (size_t)written != len) {
        return ROBOT_ERR_AUDIO;
    }
    return ROBOT_OK;
}

/** Envía a la música el volumen que corresponde ahora mismo (atenuado si
 *  hay una notificación sonando). Llamar con g_audio_lock tomado. */
static robot_status_t send_music_volume_locked(void)
{
    unsigned vol = g_audio_volume;
    if (g_music_ducked) {
        vol = vol * AUDIO_DUCK_PERCENT / 100;
    }
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "VOLUME %u\n", vol);
    return send_command(cmd);
}

static double monotonic_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* ==========================================================================
 * API pública -- US-303
 * ========================================================================== */

robot_status_t audio_play_file(const char *filepath)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;
    if (filepath == NULL || filepath[0] == '\0') return ROBOT_ERR_INVALID_PARAM;

    if (access(filepath, F_OK) != 0) {
        return ROBOT_ERR_AUDIO_FILE_NOT_FOUND;
    }

    char cmd[512];
    int n = snprintf(cmd, sizeof(cmd), "LOAD %s\n", filepath);
    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        return ROBOT_ERR_INVALID_PARAM;
    }

    pthread_mutex_lock(&g_audio_lock);
    robot_status_t status = send_command(cmd);
    if (status == ROBOT_OK) {
        strncpy(g_current_file, filepath, sizeof(g_current_file) - 1);
        g_current_file[sizeof(g_current_file) - 1] = '\0';
        g_audio_state = AUDIO_PLAYING;
    }
    pthread_mutex_unlock(&g_audio_lock);

    return status;
}

/** Hilo desacoplado que espera a que termine UNA notificación y, si sigue
 *  siendo la notificación vigente, restaura el volumen de la música. Se
 *  usa un hilo porque la biblioteca no tiene un "tick" propio desde el
 *  cual hacer waitpid(WNOHANG). */
static void *notification_waiter(void *arg)
{
    pid_t pid = (pid_t)(intptr_t)arg;
    waitpid(pid, NULL, 0); /* bloquea solo a este hilo */

    pthread_mutex_lock(&g_audio_lock);
    if (g_notif_pid == pid) {
        g_notif_pid = -1;
        if (g_music_ducked) {
            g_music_ducked = false;
            send_music_volume_locked();
        }
    }
    /* Si g_notif_pid != pid, la notificación fue reemplazada por otra más
     * reciente: esa otra se encargará de restaurar el volumen. */
    pthread_mutex_unlock(&g_audio_lock);
    return NULL;
}

/** fork+exec de `mpg123 -q <file>` sin bloquear. Devuelve el pid o -1. */
static pid_t spawn_notification(const char *file)
{
    pid_t pid = fork();
    if (pid != 0) return pid; /* padre, o -1 si fork falló */

    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > STDERR_FILENO) close(devnull);
    }
    /* No heredar sockets del servidor ni el pipe hacia la música */
    for (int fd = 3; fd < 1024; fd++) close(fd);

    execlp("mpg123", "mpg123", "-q", file, (char *)NULL);
    _exit(127);
}

/**
 * Reproduce la notificación SIN interrumpir la música:
 *  1. Cooldown de AUDIO_NOTIF_COOLDOWN_S por evento (devuelve ROBOT_OK sin
 *     sonar si aún no pasa, p.ej. alertas de obstáculo repetidas).
 *  2. Si ya suena otra notificación, se corta (gana la más reciente).
 *  3. Si la música está sonando se atenúa (ducking).
 *  4. Se lanza un `mpg123 -q` aparte y un hilo que espera su fin para
 *     restaurar el volumen.
 * Requiere que ALSA permita dos streams simultáneos (dmix / PipeWire).
 */
robot_status_t audio_play_notification(audio_event_t event)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;
    if (event < AUDIO_EVENT_SYSTEM_START || event > AUDIO_EVENT_MANUAL_MODE) {
        return ROBOT_ERR_INVALID_PARAM;
    }

    const char *file = g_notification_files[event];
    if (access(file, R_OK) != 0) {
        return ROBOT_ERR_AUDIO_FILE_NOT_FOUND;
    }

    pthread_mutex_lock(&g_audio_lock);

    double now = monotonic_now();
    if (g_last_notif_time[event] > 0.0 &&
        now - g_last_notif_time[event] < AUDIO_NOTIF_COOLDOWN_S) {
        pthread_mutex_unlock(&g_audio_lock);
        return ROBOT_OK; /* omitida por cooldown: no es un error */
    }
    g_last_notif_time[event] = now;

    if (g_notif_pid > 0) {
        /* Su hilo waiter lo recogerá; al ver que ya no es la vigente no
         * tocará el volumen. */
        kill(g_notif_pid, SIGTERM);
        g_notif_pid = -1;
    }

    if (g_audio_state == AUDIO_PLAYING && !g_music_ducked) {
        g_music_ducked = true;
        send_music_volume_locked();
    }

    robot_status_t status = ROBOT_OK;
    pid_t pid = spawn_notification(file);
    if (pid < 0) {
        status = ROBOT_ERR_AUDIO;
    } else {
        g_notif_pid = pid;
        pthread_t th;
        if (pthread_create(&th, NULL, notification_waiter, (void *)(intptr_t)pid) == 0) {
            pthread_detach(th);
        } else {
            /* Sin hilo no habría quién restaure: se cancela la notificación */
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            g_notif_pid = -1;
            status = ROBOT_ERR_AUDIO;
        }
    }

    if (status != ROBOT_OK && g_music_ducked) {
        g_music_ducked = false;
        send_music_volume_locked();
    }

    pthread_mutex_unlock(&g_audio_lock);
    return status;
}

robot_status_t audio_pause(void)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;

    pthread_mutex_lock(&g_audio_lock);
    robot_status_t status = ROBOT_OK;
    if (g_audio_state == AUDIO_PLAYING) {
        /* En el protocolo remoto de mpg123, PAUSE alterna entre pausado
         * y reproduciendo -- por eso pause/resume mandan el mismo comando,
         * distinguiendo el efecto por el estado que llevamos localmente. */
        status = send_command("PAUSE\n");
        if (status == ROBOT_OK) g_audio_state = AUDIO_PAUSED;
    }
    pthread_mutex_unlock(&g_audio_lock);

    return status;
}

robot_status_t audio_resume(void)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;

    pthread_mutex_lock(&g_audio_lock);
    robot_status_t status = ROBOT_OK;
    if (g_audio_state == AUDIO_PAUSED) {
        status = send_command("PAUSE\n");
        if (status == ROBOT_OK) g_audio_state = AUDIO_PLAYING;
    }
    pthread_mutex_unlock(&g_audio_lock);

    return status;
}

robot_status_t audio_stop(void)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;

    pthread_mutex_lock(&g_audio_lock);
    robot_status_t status = send_command("STOP\n");
    if (status == ROBOT_OK) {
        g_audio_state = AUDIO_STOPPED;
        g_current_file[0] = '\0';
    }
    pthread_mutex_unlock(&g_audio_lock);

    return status;
}

robot_status_t audio_set_volume(uint8_t volume_percent)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;
    if (volume_percent > 100) return ROBOT_ERR_INVALID_PARAM;

    pthread_mutex_lock(&g_audio_lock);
    uint8_t previous = g_audio_volume;
    g_audio_volume = volume_percent;
    /* Si hay notificación sonando se manda el valor atenuado; el volumen
     * completo se restaura cuando termina la notificación. */
    robot_status_t status = send_music_volume_locked();
    if (status != ROBOT_OK) g_audio_volume = previous;
    pthread_mutex_unlock(&g_audio_lock);

    return status;
}

robot_status_t audio_get_volume(uint8_t *volume_percent)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;
    if (volume_percent == NULL) return ROBOT_ERR_INVALID_PARAM;

    pthread_mutex_lock(&g_audio_lock);
    *volume_percent = g_audio_volume;
    pthread_mutex_unlock(&g_audio_lock);

    return ROBOT_OK;
}

robot_status_t audio_get_state(audio_state_t *state)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;
    if (state == NULL) return ROBOT_ERR_INVALID_PARAM;

    pthread_mutex_lock(&g_audio_lock);
    *state = g_audio_state;
    pthread_mutex_unlock(&g_audio_lock);

    return ROBOT_OK;
}

robot_status_t audio_list_files(char out_paths[][256],
                                 uint32_t max_count,
                                 uint32_t *out_found)
{
    if (!g_robot_initialized) return ROBOT_ERR_NOT_INITIALIZED;
    if (out_paths == NULL || out_found == NULL || max_count == 0) {
        return ROBOT_ERR_INVALID_PARAM;
    }

    DIR *dir = opendir(AUDIO_MUSIC_DIR);
    if (dir == NULL) {
        *out_found = 0;
        return ROBOT_ERR_AUDIO_FILE_NOT_FOUND;
    }

    uint32_t count = 0;
    struct dirent *entry;
    while (count < max_count && (entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        size_t len = strlen(name);
        if (len > 4 && strcasecmp(name + len - 4, ".mp3") == 0) {
            int written = snprintf(out_paths[count], 256, "%s/%s", AUDIO_MUSIC_DIR, name);
            if (written > 0 && written < 256) {
                count++; /* nombre demasiado largo para el buffer: se omite */
            }
        }
    }
    closedir(dir);

    *out_found = count;
    return ROBOT_OK;
}

/* ==========================================================================
 * Init / cleanup internos (llamados por librobot.c)
 * ========================================================================== */

robot_status_t audio_module_init(void)
{
    /* Si mpg123 muere (o nunca llegó a existir por un execlp fallido),
     * escribir en el pipe hacia él generaría SIGPIPE y mataría todo el
     * proceso del robot por default -- se ignora a propósito. */
    signal(SIGPIPE, SIG_IGN);

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        return ROBOT_ERR_AUDIO;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return ROBOT_ERR_AUDIO;
    }

    if (pid == 0) {
        /* Proceso hijo: se convierte en mpg123 en modo de control remoto. */
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        execlp("mpg123", "mpg123", "-R", "--quiet", (char *)NULL);
        _exit(127); /* solo se llega aquí si execlp falló */
    }

    /* Proceso padre */
    close(pipefd[0]);
    /* Que otros procesos lanzados después (p.ej. notificaciones) no
     * hereden el extremo de escritura del pipe hacia la música. */
    fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);
    g_mpg123_stdin_fd = pipefd[1];
    g_mpg123_pid = pid;
    g_audio_state = AUDIO_STOPPED;
    g_audio_volume = 80;
    g_current_file[0] = '\0';
    g_notif_pid = -1;
    g_music_ducked = false;
    memset(g_last_notif_time, 0, sizeof(g_last_notif_time));

    return ROBOT_OK;
}

/**
 * IMPORTANTE: al igual que el resto de módulos, corre DESPUÉS de que
 * g_robot_initialized ya es false, así que no pasa por las funciones
 * públicas de arriba (que están gateadas por ese flag).
 */
void audio_module_cleanup(void)
{
    /* Cortar la notificación en curso. Su hilo waiter la recoge con
     * waitpid() y, al ver g_notif_pid == -1, no toca nada más. */
    pthread_mutex_lock(&g_audio_lock);
    if (g_notif_pid > 0) {
        kill(g_notif_pid, SIGTERM);
        g_notif_pid = -1;
    }
    g_music_ducked = false;
    pthread_mutex_unlock(&g_audio_lock);

    if (g_mpg123_stdin_fd >= 0) {
        write(g_mpg123_stdin_fd, "QUIT\n", 5); /* best-effort, se ignora error */
        close(g_mpg123_stdin_fd);
        g_mpg123_stdin_fd = -1;
    }

    if (g_mpg123_pid > 0) {
        int status;
        int waited_ms = 0;
        pid_t r;
        do {
            r = waitpid(g_mpg123_pid, &status, WNOHANG);
            if (r == g_mpg123_pid) break;
            struct timespec ts = { .tv_sec = 0, .tv_nsec = 50000000L }; /* 50ms */
            nanosleep(&ts, NULL);
            waited_ms += 50;
        } while (waited_ms < 1000);

        if (r != g_mpg123_pid) {
            kill(g_mpg123_pid, SIGKILL);
            waitpid(g_mpg123_pid, &status, 0);
        }
        g_mpg123_pid = -1;
    }

    g_audio_state = AUDIO_STOPPED;
    g_current_file[0] = '\0';
}
