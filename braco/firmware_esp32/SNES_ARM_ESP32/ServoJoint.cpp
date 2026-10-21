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

    float clampf(float value, float lo, float hi)
    {
        return value < lo ? lo : (value > hi ? hi : value);
    }
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

    /* No boot o servo vai direto para home: não há como saber onde ele estava. */
    currentDeg = targetDeg = cfg->homeDeg;
    write(currentDeg);
}

void ServoJoint::setTarget(float deg)
{
    targetDeg = clampf(deg, cfg->minDeg, cfg->maxDeg);
}

void ServoJoint::hold()
{
    targetDeg = currentDeg;
}

void ServoJoint::update(float dt, float maxSpeed)
{
    const float step = maxSpeed * dt;
    const float error = targetDeg - currentDeg;

    if (error == 0.0f)
        return;

    if (fabsf(error) <= step)
        currentDeg = targetDeg;
    else
        currentDeg += (error > 0.0f) ? step : -step;

    write(currentDeg);
}

void ServoJoint::write(float deg)
{
    const float logical = cfg->inverted ? 180.0f - deg : deg;
    const float us = cfg->pulseMinUs + logical / 180.0f * (cfg->pulseMaxUs - cfg->pulseMinUs);
    const uint32_t duty = static_cast<uint32_t>(us / PERIOD_US * DUTY_MAX + 0.5f);

#if LEDC_API_V3
    ledcWrite(cfg->pin, duty);
#else
    ledcWrite(channel, duty);
#endif
}
