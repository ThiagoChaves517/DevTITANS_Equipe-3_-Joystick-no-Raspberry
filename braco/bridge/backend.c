#include "backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct backend
{
    void *impl;
    const struct backend_ops *ops;
};

/* ---- "none": só imprime quando o alvo muda ---- */

struct none_backend
{
    int last[JOINT_COUNT];
    bool printed;
};

static void none_update(void *impl, const float target[JOINT_COUNT], uint32_t now_ms)
{
    struct none_backend *b = impl;
    int now[JOINT_COUNT];
    bool changed = !b->printed;

    for (int j = 0; j < JOINT_COUNT; ++j)
    {
        now[j] = (int)(target[j] + 0.5f);
        changed |= now[j] != b->last[j];
    }

    if (!changed)
        return;

    printf("[NONE] t=%u J %d %d %d %d\n", now_ms, now[0], now[1], now[2], now[3]);
    memcpy(b->last, now, sizeof(now));
    b->printed = true;
}

static void none_note(void *impl, const char *text)
{
    (void)impl;
    printf("[NONE] # %s\n", text);
}

static const char *none_status(void *impl)
{
    (void)impl;
    return "dry-run";
}

static void none_close(void *impl)
{
    free(impl);
}

static const struct backend_ops none_ops =
{
    none_update, none_note, NULL, none_status, none_close
};

/* ---- fábrica e despacho ---- */

struct backend *backend_open(const char *kind, const struct backend_options *opt)
{
    struct backend *b = calloc(1, sizeof(*b));

    if (b == NULL)
        return NULL;

    if (strcmp(kind, "serial") == 0)
        b->impl = backend_serial_open(opt, &b->ops);
    else if (strcmp(kind, "tcp") == 0)
        b->impl = backend_tcp_open(opt, &b->ops);
    else if (strcmp(kind, "gpio") == 0)
        b->impl = backend_gpio_open(opt, &b->ops);
    else if (strcmp(kind, "none") == 0)
    {
        b->impl = calloc(1, sizeof(struct none_backend));
        b->ops = &none_ops;
    }
    else
        fprintf(stderr, "[BACKEND] tipo desconhecido: %s\n", kind);

    if (b->impl == NULL)
    {
        free(b);
        return NULL;
    }

    return b;
}

void backend_update(struct backend *b, const float target[JOINT_COUNT], uint32_t now_ms)
{
    b->ops->update(b->impl, target, now_ms);
}

void backend_note(struct backend *b, const char *text)
{
    b->ops->note(b->impl, text);
}

bool backend_take_limits(struct backend *b, struct backend_limits *out)
{
    struct text_link *link = b->ops->link ? b->ops->link(b->impl) : NULL;

    if (link == NULL || !link->limits_new)
        return false;

    link->limits_new = false;
    *out = link->limits;
    return true;
}

bool backend_ready(struct backend *b)
{
    struct text_link *link = b->ops->link ? b->ops->link(b->impl) : NULL;

    return link == NULL || (link->fd >= 0 && link->started && link->synced);
}

bool backend_take_sync(struct backend *b, float out[JOINT_COUNT])
{
    struct text_link *link = b->ops->link ? b->ops->link(b->impl) : NULL;

    if (link == NULL || !link->sync_new)
        return false;

    link->sync_new = false;
    memcpy(out, link->telemetry, sizeof(link->telemetry));
    return true;
}

bool backend_telemetry(struct backend *b, float out[JOINT_COUNT])
{
    struct text_link *link = b->ops->link ? b->ops->link(b->impl) : NULL;

    if (link == NULL || !link->have_telemetry)
        return false;

    memcpy(out, link->telemetry, sizeof(link->telemetry));
    return true;
}

const char *backend_status(struct backend *b)
{
    return b->ops->status(b->impl);
}

void backend_close(struct backend *b)
{
    if (b == NULL)
        return;

    b->ops->close(b->impl);
    free(b);
}
