#pragma once
#include <Arduino.h>
#include "ButtonInput.h"
#include "AccelerometerInput.h"
class InputManager {
public:
    enum InputMode : uint8_t { BUTTON_MODE, ACCELEROMETER_MODE };
    void begin();
    uint16_t readState();
    bool isSelectPressed() const;
    void setMode(InputMode mode);
    InputMode mode() const { return currentMode; }
    uint8_t readGripState();
    void printDiagnostics();
private:
    ButtonInput buttons;
    AccelerometerInput accelerometer;
    InputMode currentMode=BUTTON_MODE;
    uint16_t readButtonState();
    void printButtonTransitions();
    bool debugInitialized = false;
    bool lastB = false, lastY = false, lastSelect = false;
    bool lastStart = false, lastA = false, lastX = false;
};
