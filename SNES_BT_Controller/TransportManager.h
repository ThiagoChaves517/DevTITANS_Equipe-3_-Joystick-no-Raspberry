#pragma once
#include <Arduino.h>
#include "BleService.h"
#include "UartTransport.h"
class TransportManager {
public:
    enum Mode : uint8_t { BLUETOOTH, WIRED };
    enum Action : uint8_t { NONE, TOGGLE_INPUT, CALIBRATE_ACCEL };
    void begin(BleService* bleService);
    Action update(uint16_t state, bool selectPressed, bool startPressed, uint8_t gripState);
    Mode mode() const { return currentMode; }
    void sendFrame(uint8_t type, uint8_t p0, uint8_t p1);
private:
    void switchTransport();
    BleService* ble=nullptr;
    UartTransport uart;
    Mode currentMode=BLUETOOTH;
    bool rawSelect=false, stableSelect=false, longPressHandled=false;
    bool rawStart=false, stableStart=false;
    uint32_t rawChangedAt=0, selectPressedAt=0, rawStartChangedAt=0;
    uint8_t sequence=0;
};
