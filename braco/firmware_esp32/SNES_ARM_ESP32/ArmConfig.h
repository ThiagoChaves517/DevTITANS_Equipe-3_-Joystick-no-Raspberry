#pragma once

#include <Arduino.h>

/*
 * Configuração do braço MeArm de acrílico (4 DOF, 4x SG90).
 *
 * CALIBRAR com o braço montado antes de usar (ver braco/README.md):
 * mande "J ..." pelo Monitor Serial movendo uma junta por vez em passos
 * pequenos e anote onde a peça encosta. min/max ficam alguns graus antes
 * do encosto: servo forçando contra a estrutura esquenta e queima.
 *
 * Os mesmos números estão em braco/bridge/arm_model.c e no simulador,
 * mas quem manda é este arquivo: a bridge pede os limites com o comando I.
 */

namespace ArmConfig
{
    constexpr uint32_t SERIAL_BAUD = 115200;

    /* Sem quadro J por este tempo -> servos congelam onde estão. */
    constexpr uint32_t WATCHDOG_MS = 300;

    constexpr uint32_t TELEMETRY_MS = 100; /* T a 10 Hz */
    constexpr uint32_t UPDATE_MS = 5;      /* rampa a 200 Hz */

    /* Velocidade máxima da rampa (protege de saltos) e velocidade do home. */
    constexpr float MAX_SPEED_DEG_S = 180.0f;
    constexpr float HOME_SPEED_DEG_S = 60.0f;

    /* Servo: 50 Hz, pulso de 500 µs (0°) a 2400 µs (180°) no SG90. */
    constexpr uint32_t SERVO_FREQ_HZ = 50;
    constexpr uint8_t  PWM_RESOLUTION_BITS = 16;

    struct Joint
    {
        const char *name;
        uint8_t  pin;
        float    minDeg;
        float    maxDeg;
        float    homeDeg;
        uint16_t pulseMinUs;
        uint16_t pulseMaxUs;
        bool     inverted; /* servo montado espelhado: 0° <-> 180° */
    };

    /*
     * GPIOs da wiki "Braço Robótico - Semana 09/09-21/10" (William), exceto a
     * garra: a wiki usa GPIO12, mas ele é pino de strapping do ESP32 (define a
     * tensão da flash no boot) e um sinal alto ali pode impedir o boot. Aqui
     * a garra fica no GPIO13, que é livre e vizinho na placa.
     */
    constexpr Joint JOINTS[4] =
    {
        { "base",     25, 10.0f, 170.0f, 90.0f, 500, 2400, false },
        { "ombro",    26, 45.0f, 135.0f, 90.0f, 500, 2400, false },
        { "cotovelo", 27, 45.0f, 135.0f, 90.0f, 500, 2400, false },
        { "garra",    13, 30.0f,  90.0f, 60.0f, 500, 2400, false },
    };

    constexpr uint8_t JOINT_COUNT = sizeof(JOINTS) / sizeof(JOINTS[0]);

    constexpr const char *FIRMWARE_VERSION = "SNES-ARM-ESP32 1.0";
}
