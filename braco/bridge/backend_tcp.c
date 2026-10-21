#include "backend.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/*
 * Simulador do braço (braco/simulador/arm_sim.py), mesmo protocolo de texto
 * do ESP32. Na Raspberry, use `adb reverse tcp:7878 tcp:7878` para que
 * 127.0.0.1:7878 na placa chegue no simulador rodando no computador.
 */

#define RETRY_MS 1000u

struct tcp_backend
{
    char host[64];
    int  port;
    uint32_t last_try_ms;
    bool tried_once;
    bool warned;
    struct text_link link;
    char status[96];
};

static int connect_tcp(const char *host, int port)
{
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;

    if (getaddrinfo(host, port_str, &hints, &res) != 0 || res == NULL)
        return -1;

    const int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    if (fd < 0 || connect(fd, res->ai_addr, res->ai_addrlen) < 0)
    {
        if (fd >= 0)
            close(fd);

        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);

    const int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

    return fd;
}

static void tcp_update(void *impl, const float target[JOINT_COUNT], uint32_t now_ms)
{
    struct tcp_backend *b = impl;

    if (b->link.fd < 0)
    {
        if (b->tried_once && (uint32_t)(now_ms - b->last_try_ms) < RETRY_MS)
            return;

        b->tried_once = true;
        b->last_try_ms = now_ms;

        const int fd = connect_tcp(b->host, b->port);

        if (fd < 0)
        {
            if (!b->warned)
            {
                printf("[TCP] simulador indisponível em %s:%d (%s); tentando a cada %u ms\n",
                       b->host, b->port, strerror(errno), RETRY_MS);
                b->warned = true;
            }

            return;
        }

        printf("[TCP] conectado ao simulador %s:%d\n", b->host, b->port);
        link_attach(&b->link, fd, now_ms);
        b->warned = false;
    }

    if (!link_poll(&b->link, now_ms))
        return;

    link_send_targets(&b->link, target, now_ms);
}

static void tcp_note(void *impl, const char *text)
{
    struct tcp_backend *b = impl;
    char line[96];

    snprintf(line, sizeof(line), "# %s", text);
    link_send_line(&b->link, line);
}

static struct text_link *tcp_link(void *impl)
{
    return &((struct tcp_backend *)impl)->link;
}

static const char *tcp_status(void *impl)
{
    struct tcp_backend *b = impl;

    snprintf(b->status, sizeof(b->status), "tcp: %s:%d %s", b->host, b->port,
             b->link.fd < 0 ? "desconectado" : "conectado");
    return b->status;
}

static void tcp_close(void *impl)
{
    struct tcp_backend *b = impl;

    if (b->link.fd >= 0)
    {
        link_send_line(&b->link, "S");
        link_detach(&b->link, "bridge encerrada");
    }

    free(b);
}

static const struct backend_ops tcp_ops =
{
    tcp_update, tcp_note, tcp_link, tcp_status, tcp_close
};

void *backend_tcp_open(const struct backend_options *opt, const struct backend_ops **ops)
{
    struct tcp_backend *b = calloc(1, sizeof(*b));

    if (b == NULL)
        return NULL;

    snprintf(b->host, sizeof(b->host), "%s", opt->host ? opt->host : "127.0.0.1");
    b->port = opt->port > 0 ? opt->port : 7878;

    link_init(&b->link, "[TCP]", opt->verbose);

    *ops = &tcp_ops;
    return b;
}
