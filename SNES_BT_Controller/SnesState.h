#pragma once

#include <Arduino.h>

namespace Snes
{
    enum ButtonBit : uint8_t
    {
        B      = 0,
        Y      = 1,
        SELECT = 2,
        START  = 3,

        UP     = 4,
        DOWN   = 5,
        LEFT   = 6,
        RIGHT  = 7,

        A      = 8,
        X      = 9,

        L      = 10,
        R      = 11
    };

    constexpr uint16_t ALL_RELEASED = 0xFFFF;
}