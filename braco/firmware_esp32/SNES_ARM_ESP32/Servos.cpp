#include "Servos.h"

#include "ArmConfig.h"

#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

/* O core ESP32 3.x trocou a API do LEDC (canal -> pino). */
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
#define LEDC_API_V3 1
#else
#define LEDC_API_V3 0
#endif

namespace
{
    constexpr uint32_t PERIODO_US = 1000000UL / ArmConfig::SERVO_FREQ_HZ;
    constexpr uint32_t DUTY_MAX = (1UL << ArmConfig::PWM_RESOLUTION_BITS) - 1;
}

void servoIniciar(uint8_t pino, uint8_t canal, int angulo, bool inverte)
{
#if LEDC_API_V3
    (void)canal;
    ledcAttach(pino, ArmConfig::SERVO_FREQ_HZ, ArmConfig::PWM_RESOLUTION_BITS);
#else
    ledcSetup(canal, ArmConfig::SERVO_FREQ_HZ, ArmConfig::PWM_RESOLUTION_BITS);
    ledcAttachPin(pino, canal);
#endif

    servoEscrever(pino, canal, angulo, inverte);
}

void servoEscrever(uint8_t pino, uint8_t canal, int angulo, bool inverte)
{
    const int fisico = inverte ? 180 - angulo : angulo;
    const float us = ArmConfig::PULSO_MIN_US + fisico / 180.0f * (ArmConfig::PULSO_MAX_US - ArmConfig::PULSO_MIN_US);
    const uint32_t duty = static_cast<uint32_t>(us / PERIODO_US * DUTY_MAX + 0.5f);

#if LEDC_API_V3
    (void)canal;
    ledcWrite(pino, duty);
#else
    (void)pino;
    ledcWrite(canal, duty);
#endif
}
