#pragma once
#include <Arduino.h>

namespace Board {
    constexpr uint8_t BUTTON_B      = 4;
    constexpr uint8_t BUTTON_Y      = 5;
    constexpr uint8_t BUTTON_SELECT = 13;
    constexpr uint8_t BUTTON_START  = 33; //alterar
    constexpr uint8_t BUTTON_A      = 16;
    constexpr uint8_t BUTTON_X      = 17;

    constexpr uint8_t ACCEL_X = 34; //alterar ADC1_CH4 
    constexpr uint8_t ACCEL_Y = 35; //alterar ADC1_CH7 (entrada-only)
    constexpr uint8_t ACCEL_Z = 32; //alterar ADC1_CH6 (entrada-only)

    // ADC2: so funciona com o Wi-Fi desligado (o firmware usa apenas BLE).
    constexpr uint8_t JOY_X = 25; //alterar ADC2_CH8, potenciometro esquerda/direita
    constexpr uint8_t JOY_Y = 26; //alterar ADC2_CH9, potenciometro cima/baixo

    constexpr uint8_t SNES_LATCH = 27;
    constexpr uint8_t SNES_CLOCK = 14;
    constexpr uint8_t SNES_DATA  = 12;
}
