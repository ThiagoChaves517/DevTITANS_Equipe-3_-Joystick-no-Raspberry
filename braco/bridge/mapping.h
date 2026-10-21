#pragma once

/*
 * Botões do SNES -> sentido de movimento de cada junta.
 *
 * O controle atual só tem A, B, X, Y, SELECT e START, então as 4 juntas
 * são divididas em duas páginas, alternadas pelo START:
 *
 *   Página 1 (COTOVELO/GARRA)    Página 2 (BASE/OMBRO)
 *   B = cotovelo sobe            B = ombro sobe
 *   Y = cotovelo desce           Y = ombro desce
 *   A = abre a garra             A = base gira (+)
 *   X = fecha a garra            X = base gira (-)
 *
 * D-pad (se existir no controle): sempre base (←/→) e ombro (↑/↓).
 * START curto: troca de página. START segurado 1,5 s: volta para home.
 * SELECT: ignorado (é usado pelo firmware do joystick).
 *
 * No modo acelerômetro o firmware do joystick transforma inclinação em
 * X/B/A/Y, então a inclinação segue a página ativa automaticamente.
 */

#include <stdbool.h>
#include <stdint.h>

#include "arm_model.h"
#include "pad.h"

#define MAPPER_START_LONG_MS 1500u

enum mapper_page
{
    PAGE_COTOVELO_GARRA = 1,
    PAGE_BASE_OMBRO     = 2
};

enum mapper_event
{
    MAPPER_EVENT_PAGE_CHANGED = 1u << 0,
    MAPPER_EVENT_HOME         = 1u << 1
};

struct mapper
{
    enum mapper_page page;
    bool     start_down;
    bool     start_long_fired;
    uint32_t start_down_ms;
    bool     wait_release; /* depois de trocar de página, espera soltar A/B/X/Y */
};

struct mapper_output
{
    float    dir[JOINT_COUNT];
    uint32_t events;
};

void mapper_init(struct mapper *m);

void mapper_update(struct mapper *m,
                   const struct pad_state *pad,
                   uint32_t now_ms,
                   struct mapper_output *out);

const char *mapper_page_name(enum mapper_page page);
