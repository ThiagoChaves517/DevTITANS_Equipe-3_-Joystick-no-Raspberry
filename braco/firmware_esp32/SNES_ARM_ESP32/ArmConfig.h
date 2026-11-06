#pragma once

#include <Arduino.h>

/*
 * Configuração do braço (4x SG90). Fonte: wiki "Braço Robótico ‐ Semana 09\09‐21\10"
 * (pinos) e o fluxograma do William em resources/ (tempos, limites e ângulos iniciais).
 *
 * Os limites são o EXEMPLO do fluxograma: calibrar com o braço montado (ver
 * braco/README.md). Servo forçando contra a estrutura esquenta e queima.
 */

namespace ArmConfig
{
    constexpr uint32_t SERIAL_BAUD = 115200;

    /* A cada INTERVALO_MS cada servo anda PASSO_GRAUS na sua direção (fluxograma: 1 grau a cada 25 ms). */
    constexpr uint32_t INTERVALO_MS = 25;
    constexpr int PASSO_GRAUS = 1;

    /*
     * Segurança (fluxograma, "tratamento de falhas"): sem quadro válido por este tempo,
     * as direções viram 0. A Raspberry reenvia o estado a cada 100 ms enquanto há botão
     * pressionado, então 250 ms tolera a perda de 1 ou 2 quadros.
     */
    constexpr uint32_t TIMEOUT_SERIAL_MS = 250;

    /* Servo: 50 Hz, pulso de 500 µs (0°) a 2400 µs (180°) no SG90. */
    constexpr uint32_t SERVO_FREQ_HZ = 50;
    constexpr uint8_t PWM_RESOLUTION_BITS = 16;

    struct Joint
    {
        const char *name;
        uint8_t pin;
        int minDeg;
        int maxDeg;
        int initialDeg;
        uint16_t pulseMinUs;
        uint16_t pulseMaxUs;
        bool inverted; /* servo montado espelhado: a direção +1 diminui o ângulo físico */
    };

    /*
     * GPIOs da wiki/diagrama do William. Atenção ao GPIO12 (garra): é pino de
     * strapping do ESP32 (tensão da flash no boot). Se a placa não ligar com a
     * garra conectada, desligue o fio do GPIO12 durante o boot ou troque de pino.
     */
    constexpr Joint JOINTS[4] =
    {
        { "base",     25,  0, 180, 90, 500, 2400, false },
        { "ombro",    26, 20, 160, 90, 500, 2400, false },
        { "cotovelo", 27, 20, 160, 90, 500, 2400, false },
        { "garra",    12, 20, 100, 60, 500, 2400, false },
    };

    constexpr uint8_t JOINT_COUNT = sizeof(JOINTS) / sizeof(JOINTS[0]);

    constexpr const char *FIRMWARE_VERSION = "SNES-ARM-ESP32 2.0";
}
