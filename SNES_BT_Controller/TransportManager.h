#pragma once
#include <Arduino.h>
#include "BleService.h"
#include "UartTransport.h"
class TransportManager {
public:
    enum Mode : uint8_t { BLUETOOTH, WIRED };
    void begin(BleService* bleService);
    bool update(uint16_t state, bool selectPressed, uint8_t gripState);
    Mode mode() const { return currentMode; }
private:
    void switchTransport();
    void sendFrame(uint8_t type, uint8_t p0, uint8_t p1);
    BleService* ble=nullptr;
    UartTransport uart;
    Mode currentMode=BLUETOOTH;
    bool rawSelect=false, stableSelect=false, longPressHandled=false;
    uint32_t rawChangedAt=0, selectPressedAt=0;
    uint8_t sequence=0;
};
