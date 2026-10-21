#pragma once

#include <Arduino.h>

class SnesEngine
{
public:
    void begin();

    void sendFrame(uint16_t state);

private:
    void sendBit(uint8_t bit);
};