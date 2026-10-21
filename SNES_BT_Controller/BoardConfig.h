#pragma once
#include <Arduino.h>

namespace Board {
    constexpr uint8_t BUTTON_B      = 19;
    constexpr uint8_t BUTTON_Y      = 5;
    constexpr uint8_t BUTTON_SELECT = 21;
    constexpr uint8_t BUTTON_START  = 33;
    constexpr uint8_t BUTTON_A      = 18;
    constexpr uint8_t BUTTON_X      = 4;

    constexpr uint8_t ACCEL_X = 32; // ADC1_CH4
    constexpr uint8_t ACCEL_Y = 35; // ADC1_CH7 (entrada-only)
    constexpr uint8_t ACCEL_Z = 34; // ADC1_CH6 (entrada-only)

    constexpr uint8_t SNES_LATCH = 27;
    constexpr uint8_t SNES_CLOCK = 14;
    constexpr uint8_t SNES_DATA  = 12;
}
