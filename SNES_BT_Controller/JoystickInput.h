#pragma once
#include <Arduino.h>
// Direcional analogico (dois potenciometros): converte a posicao em bits UP/DOWN/LEFT/RIGHT.
class JoystickInput {
public:
    void begin();
    void calibrate(); // Define a posicao atual como centro; chamar com o direcional solto
    uint16_t readState();
    void printDiagnostics();
private:
    float centerX=0, centerY=0;
    uint16_t rawX=0, rawY=0;
    bool calibrated=false;
    bool lastUp=false, lastDown=false, lastLeft=false, lastRight=false;
};
