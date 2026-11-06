#pragma once

#include <Arduino.h>

/*
 * Configuração do braço (4x SG90), com os nomes do fluxograma do William
 * ("Diagrama de Funcionamento do Software da ESP32", resources/).
 *
 * Pinos: wiki "Braço Robótico ‐ Semana 09\09‐21\10" e diagrama de ligações.
 * Limites e ângulos iniciais: os EXEMPLOS do fluxograma. Calibrar com o braço
 * montado (braco/README.md): servo forçando contra a estrutura esquenta e queima.
 */

namespace ArmConfig
{
    /* Setup: "Inicia Serial (115200)". */
    constexpr uint32_t SERIAL_BAUD = 115200;

    /* "A cada 25 ms, atualiza os ângulos" / "1 grau a cada 25 ms". */
    constexpr unsigned long INTERVALO_MS = 25;
    constexpr int PASSO_GRAUS = 1;

    /* Tratamento de falhas: "zerar as direções se a comunicação serial parar por mais de 250 ms". */
    constexpr unsigned long TIMEOUT_SERIAL_MS = 250;

    /* Pinos dos servos (GPIO). GPIO12 é pino de strapping do ESP32: se a placa não
     * ligar com a garra conectada, solte o fio do GPIO12 durante o boot. */
    constexpr uint8_t PINO_BASE = 25;
    constexpr uint8_t PINO_OMBRO = 26;
    constexpr uint8_t PINO_COTOVELO = 27;
    constexpr uint8_t PINO_GARRA = 12;

    /* Limites de ângulos (exemplo do fluxograma). */
    constexpr int MIN_BASE = 0,      MAX_BASE = 180;
    constexpr int MIN_OMBRO = 20,    MAX_OMBRO = 160;
    constexpr int MIN_COTOVELO = 20, MAX_COTOVELO = 160;
    constexpr int MIN_GARRA = 20,    MAX_GARRA = 100;

    /* Ângulos iniciais (exemplo do fluxograma). */
    constexpr int INICIAL_BASE = 90;
    constexpr int INICIAL_OMBRO = 90;
    constexpr int INICIAL_COTOVELO = 90;
    constexpr int INICIAL_GARRA = 60;

    /* Servo montado espelhado: a direção +1 diminui o ângulo físico. Ajustar na calibração. */
    constexpr bool INVERTE_BASE = false;
    constexpr bool INVERTE_OMBRO = false;
    constexpr bool INVERTE_COTOVELO = false;
    constexpr bool INVERTE_GARRA = false;

    /* SG90: 50 Hz, pulso de 500 µs (0°) a 2400 µs (180°). */
    constexpr uint32_t SERVO_FREQ_HZ = 50;
    constexpr uint8_t PWM_RESOLUTION_BITS = 16;
    constexpr uint16_t PULSO_MIN_US = 500;
    constexpr uint16_t PULSO_MAX_US = 2400;

    constexpr const char *FIRMWARE_VERSION = "SNES-ARM-ESP32 2.1";
}
