#pragma once
#include <Arduino.h>
class AccelerometerInput {
public:
    void begin();
    uint16_t readState();
    void printDiagnostics();
    uint8_t readGripState(); // 0=NEUTRAL, 1=OPEN, 2=CLOSE
private:
    float filteredX=0, filteredY=0, filteredZ=0;
    float neutralX=0, neutralY=0, neutralZ=0;
    bool calibrated=false;
    uint16_t readAverage(uint8_t pin);
    bool lastMappedX=false, lastMappedB=false, lastMappedA=false, lastMappedY=false;
};
