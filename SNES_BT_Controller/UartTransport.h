#pragma once
#include <Arduino.h>

class UartTransport {
public:
    void begin(uint32_t baud = 115200);
    void sendFrame(const uint8_t* frame, size_t length);
    bool isReady() const { return ready; }
private:
    HardwareSerial* port = nullptr;
    bool ready = false;
};
