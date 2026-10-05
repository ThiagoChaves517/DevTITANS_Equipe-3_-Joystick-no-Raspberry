#pragma once

#include <Arduino.h>

class ButtonInput
{
public:

    void begin();

    bool isBPressed() const;
    bool isYPressed() const;
    bool isSelectPressed() const;
    bool isStartPressed() const;
    bool isAPressed() const;
    bool isXPressed() const;
};