#include "input.h"

#include <stdlib.h>
#include <string.h>

struct input_source
{
    void *impl;
    void (*poll)(void *impl, struct pad_state *pad, uint32_t now_ms);
    void (*close)(void *impl);
};

struct input_source *input_open(const char *spec, const char *device_name)
{
    struct input_source *src = calloc(1, sizeof(*src));

    if (src == NULL)
        return NULL;

    if (strcmp(spec, "stdin") == 0)
    {
        src->impl = input_stdin_open();
        src->poll = input_stdin_poll;
        src->close = input_stdin_close;
    }
    else
    {
        src->impl = input_evdev_open(strcmp(spec, "auto") == 0 ? NULL : spec, device_name);
        src->poll = input_evdev_poll;
        src->close = input_evdev_close;
    }

    if (src->impl == NULL)
    {
        free(src);
        return NULL;
    }

    return src;
}

void input_poll(struct input_source *src, struct pad_state *pad, uint32_t now_ms)
{
    if (src == NULL)
    {
        pad->connected = false;
        return;
    }

    src->poll(src->impl, pad, now_ms);
}

void input_close(struct input_source *src)
{
    if (src == NULL)
        return;

    src->close(src->impl);
    free(src);
}
