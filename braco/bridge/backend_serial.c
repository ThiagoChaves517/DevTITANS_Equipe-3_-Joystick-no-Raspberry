#include "backend.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/*
 * ESP32 do braço pela USB.
 *
 * O kernel da Raspberry (GloDroid 6.6.58) tem cp210x e cdc-acm, mas não
 * ch341: placas com CP2102 aparecem como /dev/ttyUSB0, ESP32-S2/S3/C3 com
 * USB nativo como /dev/ttyACM0. Com --device auto a bridge tenta os dois e
 * só fica com a porta que responder READY/OK: se o ESP32 do joystick também
 * estiver na USB, ele não é confundido com o braço.
 */

#define RETRY_MS 1000u

struct serial_backend
{
    char device[64];
    bool autodetect;
    char path[64];
    uint32_t last_try_ms;
    bool tried_once;
    bool warned;
    size_t next_candidate;
    struct text_link link;
    char status[96];
};

static int open_tty(const char *path)
{
    const int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);

    if (fd < 0)
        return -1;

    struct termios tio;

    if (tcgetattr(fd, &tio) < 0)
    {
        close(fd);
        return -1;
    }

    cfmakeraw(&tio);
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);

    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~(CSTOPB | PARENB | CRTSCTS);
    tio.c_cflag = (tio.c_cflag & ~CSIZE) | CS8;

    if (tcsetattr(fd, TCSANOW, &tio) < 0)
    {
        close(fd);
        return -1;
    }

    tcflush(fd, TCIOFLUSH);
    return fd;
}

static bool try_connect(struct serial_backend *b, uint32_t now_ms)
{
    static const char *const candidates[] =
    {
        "/dev/ttyUSB0", "/dev/ttyUSB1", "/dev/ttyACM0", "/dev/ttyACM1"
    };

    const size_t count = b->autodetect ? sizeof(candidates) / sizeof(candidates[0]) : 1;

    for (size_t k = 0; k < count; ++k)
    {
        const size_t i = (b->next_candidate + k) % count;
        const char *path = b->autodetect ? candidates[i] : b->device;
        const int fd = open_tty(path);

        if (fd < 0)
            continue;

        b->next_candidate = i + 1; /* se não for o braço, a próxima tentativa começa na seguinte */
        snprintf(b->path, sizeof(b->path), "%s", path);
        printf("[SERIAL] conectado em %s (115200 8N1), aguardando READY do ESP32...\n", path);

        link_attach(&b->link, fd, now_ms);
        b->warned = false;
        return true;
    }

    if (!b->warned)
    {
        printf("[SERIAL] nenhuma serial encontrada (%s); tentando de novo a cada %u ms\n",
               b->autodetect ? "ttyUSB0-1/ttyACM0-1" : b->device, RETRY_MS);
        b->warned = true;
    }

    return false;
}

static void serial_update(void *impl, const float target[JOINT_COUNT], uint32_t now_ms)
{
    struct serial_backend *b = impl;

    if (b->link.fd < 0)
    {
        if (b->tried_once && (uint32_t)(now_ms - b->last_try_ms) < RETRY_MS)
            return;

        b->tried_once = true;
        b->last_try_ms = now_ms;

        if (!try_connect(b, now_ms))
            return;
    }

    if (!link_poll(&b->link, now_ms))
        return;

    if (link_handshake_failed(&b->link, now_ms) && b->autodetect)
    {
        printf("[SERIAL] %s não respondeu READY/OK: não parece o ESP32 do braço\n", b->path);
        link_detach(&b->link, "ignorado");
        b->last_try_ms = now_ms - RETRY_MS; /* tenta a próxima porta já no próximo tick */
        return;
    }

    link_send_targets(&b->link, target, now_ms);
}

static void serial_note(void *impl, const char *text)
{
    struct serial_backend *b = impl;
    char line[96];

    snprintf(line, sizeof(line), "# %s", text);
    link_send_line(&b->link, line);
}

static struct text_link *serial_link(void *impl)
{
    return &((struct serial_backend *)impl)->link;
}

static const char *serial_status(void *impl)
{
    struct serial_backend *b = impl;

    if (b->link.fd < 0)
        return "serial: desconectado";

    snprintf(b->status, sizeof(b->status), "serial: %s %s", b->path,
             b->link.started ? (b->link.firmware[0] ? b->link.firmware : "ativo") : "boot");
    return b->status;
}

static void serial_close(void *impl)
{
    struct serial_backend *b = impl;

    if (b->link.fd >= 0)
    {
        link_send_line(&b->link, "S");
        tcdrain(b->link.fd);
        link_detach(&b->link, "bridge encerrada");
    }

    free(b);
}

static const struct backend_ops serial_ops =
{
    serial_update, serial_note, serial_link, serial_status, serial_close
};

void *backend_serial_open(const struct backend_options *opt, const struct backend_ops **ops)
{
    struct serial_backend *b = calloc(1, sizeof(*b));

    if (b == NULL)
        return NULL;

    snprintf(b->device, sizeof(b->device), "%s", opt->device ? opt->device : "auto");
    b->autodetect = strcmp(b->device, "auto") == 0;

    link_init(&b->link, "[SERIAL]", opt->verbose);
    b->link.require_handshake = b->autodetect;

    *ops = &serial_ops;
    return b;
}
