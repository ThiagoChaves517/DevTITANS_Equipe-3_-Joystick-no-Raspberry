#include "ServoJoint.h"

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
    constexpr uint32_t PERIOD_US = 1000000UL / ArmConfig::SERVO_FREQ_HZ;
    constexpr uint32_t DUTY_MAX = (1UL << ArmConfig::PWM_RESOLUTION_BITS) - 1;
}

void ServoJoint::begin(const ArmConfig::Joint &config, uint8_t ledcChannel)
{
    cfg = &config;
    channel = ledcChannel;

#if LEDC_API_V3
    ledcAttach(cfg->pin, ArmConfig::SERVO_FREQ_HZ, ArmConfig::PWM_RESOLUTION_BITS);
#else
    ledcSetup(channel, ArmConfig::SERVO_FREQ_HZ, ArmConfig::PWM_RESOLUTION_BITS);
    ledcAttachPin(cfg->pin, channel);
#endif

    /* "Posiciona servos na posição inicial" (fluxograma). */
    angleDeg = cfg->initialDeg;
    write();
}

bool ServoJoint::step(int8_t dir)
{
    if (dir == 0)
        return false;

    const int next = constrain(angleDeg + dir * ArmConfig::PASSO_GRAUS, cfg->minDeg, cfg->maxDeg);

    if (next == angleDeg)
        return false; /* já no limite */

    angleDeg = next;
    write();
    return true;
}

void ServoJoint::write()
{
    const int logical = cfg->inverted ? 180 - angleDeg : angleDeg;
    const float us = cfg->pulseMinUs + logical / 180.0f * (cfg->pulseMaxUs - cfg->pulseMinUs);
    const uint32_t duty = static_cast<uint32_t>(us / PERIOD_US * DUTY_MAX + 0.5f);

#if LEDC_API_V3
    ledcWrite(cfg->pin, duty);
#else
    ledcWrite(channel, duty);
#endif
}
