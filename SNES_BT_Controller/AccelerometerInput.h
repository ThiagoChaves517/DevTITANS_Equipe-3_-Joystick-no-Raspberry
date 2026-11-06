#pragma once
#include <Arduino.h>
class AccelerometerInput {
public:
    void begin();
    // Define a posicao atual como neutra. Retorna a maior variacao (max-min, ADC) entre as amostras:
    // valor alto indica controle em movimento ou eixo mal conectado.
    uint16_t calibrate();
    uint16_t readState(); // Bits UP, DOWN, LEFT, RIGHT (substitui o direcional)
    void printDiagnostics();
private:
    float filteredX=0, filteredY=0, filteredZ=0;
    float neutralX=0, neutralY=0, neutralZ=0;
    bool calibrated=false;
    uint16_t readAverage(uint8_t pin);
    bool lastUp=false, lastDown=false, lastLeft=false, lastRight=false;
};
