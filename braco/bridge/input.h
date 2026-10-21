#pragma once

/*
 * De onde vem o estado do controle.
 *
 *   "auto"              procura em /dev/input o dispositivo criado pelo
 *                       snes_uinput_daemon ("SNES-BT-Controller")
 *   "/dev/input/eventN" usa exatamente esse dispositivo
 *   "stdin"             lê comandos de texto (testes sem Raspberry/joystick):
 *                         down <BOTAO> | up <BOTAO> | hat x|y <-1|0|1>
 *                         connect | disconnect
 *                       BOTAO = A B X Y L R SELECT START
 */

#include <stdint.h>

#include "pad.h"

struct input_source;

struct input_source *input_open(const char *spec, const char *device_name);

/* Não bloqueia. Mantém *pad atualizado; reconecta sozinho quando o dispositivo volta. */
void input_poll(struct input_source *src, struct pad_state *pad, uint32_t now_ms);

void input_close(struct input_source *src);

/* Implementações (input.c escolhe uma). Cada uma guarda seu estado em "impl". */
void *input_evdev_open(const char *path, const char *device_name);
void input_evdev_poll(void *impl, struct pad_state *pad, uint32_t now_ms);
void input_evdev_close(void *impl);

void *input_stdin_open(void);
void input_stdin_poll(void *impl, struct pad_state *pad, uint32_t now_ms);
void input_stdin_close(void *impl);
