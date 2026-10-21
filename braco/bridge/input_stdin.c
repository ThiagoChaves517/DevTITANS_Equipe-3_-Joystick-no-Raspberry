#include "input.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct stdin_source
{
    char line[128];
    size_t len;
    bool eof;
    struct pad_state pad;
};

void *input_stdin_open(void)
{
    struct stdin_source *src = calloc(1, sizeof(*src));

    if (src == NULL)
        return NULL;

    src->pad.connected = true;

    const int flags = fcntl(STDIN_FILENO, F_GETFL);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    printf("[INPUT] lendo comandos do stdin (down/up/hat/connect/disconnect)\n");
    return src;
}

static void apply_command(struct stdin_source *src, char *line)
{
    char *cmd = strtok(line, " \t\r\n");

    if (cmd == NULL || cmd[0] == '#')
        return;

    char *arg1 = strtok(NULL, " \t\r\n");
    char *arg2 = strtok(NULL, " \t\r\n");

    if ((strcmp(cmd, "down") == 0 || strcmp(cmd, "up") == 0) && arg1 != NULL)
    {
        const uint32_t bit = pad_button_from_name(arg1);

        if (bit == 0)
        {
            printf("[INPUT] botão desconhecido: %s\n", arg1);
            return;
        }

        if (cmd[0] == 'd')
            src->pad.buttons |= bit;
        else
            src->pad.buttons &= ~bit;
    }
    else if (strcmp(cmd, "hat") == 0 && arg1 != NULL && arg2 != NULL)
    {
        const int value = atoi(arg2);

        if (arg1[0] == 'x')
            src->pad.hat_x = value;
        else
            src->pad.hat_y = value;
    }
    else if (strcmp(cmd, "connect") == 0)
    {
        src->pad.connected = true;
    }
    else if (strcmp(cmd, "disconnect") == 0)
    {
        src->pad.connected = false;
        src->pad.buttons = 0;
        src->pad.hat_x = src->pad.hat_y = 0;
    }
    else
    {
        printf("[INPUT] comando desconhecido: %s\n", cmd);
    }
}

void input_stdin_poll(void *impl, struct pad_state *pad, uint32_t now_ms)
{
    struct stdin_source *src = impl;

    (void)now_ms;

    while (!src->eof)
    {
        char c;
        const ssize_t n = read(STDIN_FILENO, &c, 1);

        if (n == 0)
        {
            /* Fim do script: mantém o último estado (os testes usam isso). */
            src->eof = true;
            break;
        }

        if (n < 0)
            break; /* EAGAIN: nada novo agora */

        if (c == '\n' || src->len + 1 >= sizeof(src->line))
        {
            src->line[src->len] = '\0';
            apply_command(src, src->line);
            src->len = 0;
        }
        else
        {
            src->line[src->len++] = c;
        }
    }

    *pad = src->pad;
}

void input_stdin_close(void *impl)
{
    free(impl);
}
