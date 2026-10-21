#pragma once

#include <Arduino.h>

#include "ArmConfig.h"

/*
 * Uma junta do braço: PWM de hardware (LEDC) + rampa até o ângulo-alvo.
 * O alvo sempre é saturado nos limites da junta.
 */
class ServoJoint
{
public:
    void begin(const ArmConfig::Joint &config, uint8_t channel);

    void setTarget(float deg);
    void hold(); /* alvo = posição atual */

    /* Avança dt segundos limitando a velocidade a maxSpeed (graus/s). */
    void update(float dt, float maxSpeed);

    float current() const { return currentDeg; }
    float target() const { return targetDeg; }
    const ArmConfig::Joint &config() const { return *cfg; }

private:
    void write(float deg);

    const ArmConfig::Joint *cfg = nullptr;
    uint8_t channel = 0;
    float currentDeg = 90.0f;
    float targetDeg = 90.0f;
};
