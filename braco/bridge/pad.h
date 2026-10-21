#pragma once

/*
 * Estado do controle, já independente de onde ele veio
 * (evdev do daemon do Raphael, controle falso ou script no stdin).
 */

#include <stdbool.h>
#include <stdint.h>

enum pad_button
{
    PAD_A      = 1u << 0,
    PAD_B      = 1u << 1,
    PAD_X      = 1u << 2,
    PAD_Y      = 1u << 3,
    PAD_L      = 1u << 4,
    PAD_R      = 1u << 5,
    PAD_SELECT = 1u << 6,
    PAD_START  = 1u << 7
};

struct pad_state
{
    bool     connected;
    uint32_t buttons; /* bits de pad_button: 1 = pressionado */
    int      hat_x;   /* -1 = esquerda, +1 = direita (ABS_HAT0X) */
    int      hat_y;   /* -1 = cima,     +1 = baixo   (ABS_HAT0Y) */
};

/* "A" -> PAD_A ... ; 0 se o nome não existir. */
uint32_t pad_button_from_name(const char *name);
