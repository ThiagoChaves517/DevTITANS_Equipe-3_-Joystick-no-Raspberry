#pragma once

#include <Arduino.h>

#include "ButtonInput.h"

class InputManager
{
public:

    void begin();

    uint16_t readState();

private:

    ButtonInput buttons;
};