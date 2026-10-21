#include "backend.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Protocolo Pi <-> braço (detalhes em braco/PROTOCOLO.md).
 *
 * Pi -> braço:  J <base> <ombro> <cotovelo> <garra> | G <0-100> | H | S | P | I | # texto
 * braço -> Pi:  READY <versao> | OK | ERR <motivo> | T <4 ângulos> |
 *               I <min max home> x4 | # log
 *
 * Abrir a serial de um ESP32 DevKit costuma reiniciá-lo (DTR/RTS ligados ao
 * EN/IO0). Por isso, depois de conectar, espera o READY por até BOOT_WAIT_MS
 * antes de mandar o primeiro J. Se a placa não reiniciou, o P mandado após
 * PING_AFTER_MS é respondido com OK e a espera acaba antes.
 */

#define PING_AFTER_MS 300u
#define BOOT_WAIT_MS 2500u
#define SYNC_WAIT_MS 1000u

void link_init(struct text_link *link, const char *tag, bool verbose)
{
    memset(link, 0, sizeof(*link));
    link->tag = tag;
    link->verbose = verbose;
    link->fd = -1;
}

void link_attach(struct text_link *link, int fd, uint32_t now_ms)
{
    link->fd = fd;
    link->rx_len = 0;
    link->attached_ms = now_ms;
    link->started = false;
    link->info_requested = false;
    link->ping_sent = false;
    link->have_telemetry = false;
    link->synced = false;
    link->sync_new = false;
    link->firmware[0] = '\0';
}

static void mark_started(struct text_link *link, uint32_t now_ms)
{
    if (link->started)
        return;

    link->started = true;
    link->started_ms = now_ms;
    link->synced = false;
}

void link_detach(struct text_link *link, const char *why)
{
    if (link->fd < 0)
        return;

    printf("%s desconectado (%s)\n", link->tag, why);

    close(link->fd);
    link->fd = -1;
    link->started = false;
}

bool link_send_line(struct text_link *link, const char *line)
{
    if (link->fd < 0)
        return false;

    char buf[96];
    const int len = snprintf(buf, sizeof(buf), "%s\n", line);

    const ssize_t written = write(link->fd, buf, (size_t)len);

    if (written < 0)
    {
        /* Buffer cheio: perder um quadro de 50 Hz não tem problema. */
        if (errno == EAGAIN || errno == EINTR)
            return true;

        link_detach(link, strerror(errno));
        return false;
    }

    if (link->verbose)
        printf("%s >> %s\n", link->tag, line);

    return true;
}

static void parse_line(struct text_link *link, char *line, uint32_t now_ms)
{
    if (line[0] == '\0')
        return;

    if (strncmp(line, "READY", 5) == 0)
    {
        snprintf(link->firmware, sizeof(link->firmware), "%s", line[5] ? line + 6 : "?");
        printf("%s firmware pronto: %s\n", link->tag, link->firmware);

        /* ESP32 (re)iniciou: pede os limites e espera a posição real de novo. */
        link->started = false;
        mark_started(link, now_ms);
        link->info_requested = false;
        return;
    }

    if (line[0] == 'T' && line[1] == ' ')
    {
        float t[JOINT_COUNT];

        if (sscanf(line + 2, "%f %f %f %f", &t[0], &t[1], &t[2], &t[3]) == JOINT_COUNT)
        {
            memcpy(link->telemetry, t, sizeof(t));
            link->have_telemetry = true;

            if (link->started && !link->synced)
            {
                link->synced = true;
                link->sync_new = true;
            }
        }

        return;
    }

    if (line[0] == 'I' && line[1] == ' ')
    {
        struct backend_limits l;
        float *v[12];

        for (int j = 0; j < JOINT_COUNT; ++j)
        {
            v[j * 3 + 0] = &l.min[j];
            v[j * 3 + 1] = &l.max[j];
            v[j * 3 + 2] = &l.home[j];
        }

        const int n = sscanf(line + 2, "%f %f %f %f %f %f %f %f %f %f %f %f",
                             v[0], v[1], v[2], v[3], v[4], v[5],
                             v[6], v[7], v[8], v[9], v[10], v[11]);

        if (n == 12)
        {
            link->limits = l;
            link->limits_new = true;
            mark_started(link, now_ms);
        }
        else
        {
            printf("%s resposta I inválida: %s\n", link->tag, line);
        }

        return;
    }

    if (strcmp(line, "OK") == 0 && !link->started)
    {
        printf("%s firmware respondeu ao ping (já estava rodando)\n", link->tag);
        mark_started(link, now_ms);
        return;
    }

    if (strncmp(line, "ERR", 3) == 0)
    {
        printf("%s firmware reclamou: %s\n", link->tag, line);
        return;
    }

    /* OK e "# log" do firmware: só no modo verboso. */
    if (link->verbose)
        printf("%s << %s\n", link->tag, line);
}

bool link_poll(struct text_link *link, uint32_t now_ms)
{
    if (link->fd < 0)
        return false;

    char buf[128];

    for (;;)
    {
        const ssize_t n = read(link->fd, buf, sizeof(buf));

        if (n == 0)
        {
            link_detach(link, "fim da conexão");
            return false;
        }

        if (n < 0)
        {
            if (errno == EAGAIN || errno == EINTR)
                return true;

            link_detach(link, strerror(errno));
            return false;
        }

        for (ssize_t i = 0; i < n; ++i)
        {
            const char c = buf[i];

            if (c == '\r')
                continue;

            if (c == '\n' || link->rx_len + 1 >= sizeof(link->rx))
            {
                link->rx[link->rx_len] = '\0';
                parse_line(link, link->rx, now_ms);
                link->rx_len = 0;
            }
            else
            {
                link->rx[link->rx_len++] = c;
            }
        }
    }
}

bool link_handshake_failed(const struct text_link *link, uint32_t now_ms)
{
    return link->fd >= 0 && !link->started &&
           (uint32_t)(now_ms - link->attached_ms) >= BOOT_WAIT_MS;
}

void link_send_targets(struct text_link *link, const float target[JOINT_COUNT], uint32_t now_ms)
{
    if (link->fd < 0)
        return;

    if (!link->started && !link->ping_sent && (uint32_t)(now_ms - link->attached_ms) >= PING_AFTER_MS)
    {
        link->ping_sent = true;
        link_send_line(link, "P");
    }

    if (link_handshake_failed(link, now_ms))
    {
        if (link->require_handshake)
            return; /* quem chamou decide (ex.: tentar a próxima serial) */

        printf("%s sem READY em %u ms; seguindo assim mesmo\n", link->tag, BOOT_WAIT_MS);
        mark_started(link, now_ms);
    }

    if (!link->started)
        return;

    if (!link->info_requested)
    {
        link->info_requested = true;
        link_send_line(link, "I");
    }

    /* Só manda J depois de saber onde o braço está (1ª telemetria). */
    if (!link->synced)
    {
        if ((uint32_t)(now_ms - link->started_ms) < SYNC_WAIT_MS)
            return;

        printf("%s firmware sem telemetria T; usando o alvo atual da bridge\n", link->tag);
        link->synced = true;
    }

    char line[64];

    snprintf(line, sizeof(line), "J %d %d %d %d",
             (int)lroundf(target[JOINT_BASE]),
             (int)lroundf(target[JOINT_OMBRO]),
             (int)lroundf(target[JOINT_COTOVELO]),
             (int)lroundf(target[JOINT_GARRA]));

    /* J a cada tick mesmo sem mudança: é o heartbeat do watchdog do firmware. */
    const bool verbose = link->verbose;
    link->verbose = false; /* não inundar o log com 50 linhas/s */
    link_send_line(link, line);
    link->verbose = verbose;
}
