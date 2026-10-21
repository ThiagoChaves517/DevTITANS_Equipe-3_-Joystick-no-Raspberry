#include "input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __linux__

/*
 * Lê o gamepad virtual criado pelo snes_uinput_daemon (branch driver),
 * que publica BTN_A/B/X/Y/TL/TR/SELECT/START + ABS_HAT0X/Y.
 *
 * Não usa EVIOCGRAB: o Android continua recebendo o controle normalmente.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define RESCAN_INTERVAL_MS 1000u

struct evdev_source
{
    char fixed_path[128]; /* vazio = procurar pelo nome */
    char name[128];
    char path[128];
    int  fd;
    uint32_t last_scan_ms;
    bool scanned_once;
    struct pad_state pad;
};

#define BITS_PER_LONG (sizeof(long) * 8)
#define NLONGS(n) (((n) + BITS_PER_LONG - 1) / BITS_PER_LONG)

static bool test_bit(const unsigned long *bits, int bit)
{
    return (bits[bit / BITS_PER_LONG] >> (bit % BITS_PER_LONG)) & 1ul;
}

static const struct { int code; uint32_t bit; } key_map[] =
{
    { BTN_A, PAD_A }, { BTN_B, PAD_B }, { BTN_X, PAD_X }, { BTN_Y, PAD_Y },
    { BTN_TL, PAD_L }, { BTN_TR, PAD_R },
    { BTN_SELECT, PAD_SELECT }, { BTN_START, PAD_START }
};

static uint32_t pad_bit_for_key(int code)
{
    for (size_t i = 0; i < sizeof(key_map) / sizeof(key_map[0]); ++i)
    {
        if (key_map[i].code == code)
            return key_map[i].bit;
    }

    return 0;
}

/* Lê o estado atual direto do kernel (abertura e SYN_DROPPED). */
static void resync(struct evdev_source *src)
{
    unsigned long keys[NLONGS(KEY_CNT)];

    memset(keys, 0, sizeof(keys));
    src->pad.buttons = 0;

    if (ioctl(src->fd, EVIOCGKEY(sizeof(keys)), keys) >= 0)
    {
        for (size_t i = 0; i < sizeof(key_map) / sizeof(key_map[0]); ++i)
        {
            if (test_bit(keys, key_map[i].code))
                src->pad.buttons |= key_map[i].bit;
        }
    }

    struct input_absinfo abs;

    src->pad.hat_x = ioctl(src->fd, EVIOCGABS(ABS_HAT0X), &abs) >= 0 ? abs.value : 0;
    src->pad.hat_y = ioctl(src->fd, EVIOCGABS(ABS_HAT0Y), &abs) >= 0 ? abs.value : 0;
}

/* Um gamepad serve se tiver pelo menos A e START. */
static bool looks_like_gamepad(int fd)
{
    unsigned long keys[NLONGS(KEY_CNT)];

    memset(keys, 0, sizeof(keys));

    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) < 0)
        return false;

    return test_bit(keys, BTN_A) && test_bit(keys, BTN_START);
}

static bool try_open(struct evdev_source *src, const char *path, bool require_name)
{
    const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);

    if (fd < 0)
        return false;

    char name[128] = "";
    ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);

    const bool name_ok = !require_name || strcmp(name, src->name) == 0;

    if (!name_ok || !looks_like_gamepad(fd))
    {
        close(fd);
        return false;
    }

    src->fd = fd;
    snprintf(src->path, sizeof(src->path), "%s", path);
    resync(src);
    src->pad.connected = true;

    printf("[INPUT] controle conectado: %s (\"%s\")\n", path, name);
    return true;
}

static void scan(struct evdev_source *src)
{
    if (src->fixed_path[0] != '\0')
    {
        try_open(src, src->fixed_path, false);
        return;
    }

    DIR *dir = opendir("/dev/input");

    if (dir == NULL)
        return;

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL)
    {
        if (strncmp(entry->d_name, "event", 5) != 0)
            continue;

        char path[128];
        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);

        if (try_open(src, path, true))
            break;
    }

    closedir(dir);
}

static void lose_device(struct evdev_source *src, const char *why)
{
    printf("[INPUT] controle perdido (%s): %s\n", why, src->path);

    close(src->fd);
    src->fd = -1;
    memset(&src->pad, 0, sizeof(src->pad));
}

void *input_evdev_open(const char *path, const char *device_name)
{
    struct evdev_source *src = calloc(1, sizeof(*src));

    if (src == NULL)
        return NULL;

    src->fd = -1;
    snprintf(src->fixed_path, sizeof(src->fixed_path), "%s", path ? path : "");
    snprintf(src->name, sizeof(src->name), "%s", device_name);

    printf("[INPUT] procurando controle %s\n", path ? path : device_name);
    return src;
}

void input_evdev_poll(void *impl, struct pad_state *pad, uint32_t now_ms)
{
    struct evdev_source *src = impl;

    if (src->fd < 0)
    {
        if (!src->scanned_once || (uint32_t)(now_ms - src->last_scan_ms) >= RESCAN_INTERVAL_MS)
        {
            src->scanned_once = true;
            src->last_scan_ms = now_ms;
            scan(src);
        }

        *pad = src->pad;
        return;
    }

    struct input_event ev[32];

    for (;;)
    {
        const ssize_t n = read(src->fd, ev, sizeof(ev));

        if (n < 0)
        {
            if (errno == EAGAIN || errno == EINTR)
                break;

            lose_device(src, strerror(errno));
            break;
        }

        if (n == 0)
            break;

        for (size_t i = 0; i < (size_t)n / sizeof(ev[0]); ++i)
        {
            if (ev[i].type == EV_SYN && ev[i].code == SYN_DROPPED)
            {
                resync(src);
            }
            else if (ev[i].type == EV_KEY)
            {
                const uint32_t bit = pad_bit_for_key(ev[i].code);

                if (ev[i].value)
                    src->pad.buttons |= bit;
                else
                    src->pad.buttons &= ~bit;
            }
            else if (ev[i].type == EV_ABS && ev[i].code == ABS_HAT0X)
            {
                src->pad.hat_x = ev[i].value;
            }
            else if (ev[i].type == EV_ABS && ev[i].code == ABS_HAT0Y)
            {
                src->pad.hat_y = ev[i].value;
            }
        }
    }

    *pad = src->pad;
}

void input_evdev_close(void *impl)
{
    struct evdev_source *src = impl;

    if (src->fd >= 0)
        close(src->fd);

    free(src);
}

#else /* !__linux__: build de teste no Mac só tem a entrada "stdin" */

void *input_evdev_open(const char *path, const char *device_name)
{
    (void)path;
    (void)device_name;
    fprintf(stderr, "[INPUT] evdev só existe no Linux/Android; use --input stdin\n");
    return NULL;
}

void input_evdev_poll(void *impl, struct pad_state *pad, uint32_t now_ms)
{
    (void)impl;
    (void)now_ms;
    memset(pad, 0, sizeof(*pad));
}

void input_evdev_close(void *impl)
{
    free(impl);
}

#endif
