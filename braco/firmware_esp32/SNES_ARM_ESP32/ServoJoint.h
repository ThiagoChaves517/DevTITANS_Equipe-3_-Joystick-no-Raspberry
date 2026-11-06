#pragma once

#include <Arduino.h>

#include "ArmConfig.h"

/*
 * Um servo do braço: PWM de hardware (LEDC) e ângulo inteiro dentro dos limites.
 * step(dir) é o "atualiza ângulos dos servos" do fluxograma: +1/-1 grau ou mantém.
 */
class ServoJoint
{
public:
    void begin(const ArmConfig::Joint &config, uint8_t channel);

    /* Anda PASSO_GRAUS na direção dir (-1, 0, +1). Retorna true se o ângulo mudou. */
    bool step(int8_t dir);

    int angle() const { return angleDeg; }
    const ArmConfig::Joint &config() const { return *cfg; }

private:
    void write();

    const ArmConfig::Joint *cfg = nullptr;
    uint8_t channel = 0;
    int angleDeg = 90;
};
