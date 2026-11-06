#pragma once
#include <Arduino.h>
#include "ButtonInput.h"
#include "AccelerometerInput.h"
#include "JoystickInput.h"
class InputManager {
public:
    enum InputMode : uint8_t { BUTTON_MODE, ACCELEROMETER_MODE };
    void begin();
    uint16_t readState();
    bool isSelectPressed() const;
    bool isStartPressed() const;
    uint16_t calibrateAccelerometer() { return accelerometer.calibrate(); }
    void setMode(InputMode mode);
    InputMode mode() const { return currentMode; }
    uint8_t readGripState(); // 0=NEUTRAL, 1=OPEN (A), 2=CLOSE (B)
    void printDiagnostics();
private:
    ButtonInput buttons;
    AccelerometerInput accelerometer;
    JoystickInput joystick;
    InputMode currentMode=BUTTON_MODE;
    void printButtonTransitions();
    bool debugInitialized = false;
    bool lastB = false, lastY = false, lastSelect = false;
    bool lastStart = false, lastA = false, lastX = false;
};
