#pragma once

#include <Arduino.h>
#include <BLEServer.h>
#include <BLECharacteristic.h>

class ServerCallbacks;

class BleService
{
public:
    void begin();

    void update(uint16_t state);

    bool isConnected() const;

private:
    friend class ServerCallbacks;

    BLEServer* server = nullptr;
    BLECharacteristic* stateCharacteristic = nullptr;

    bool connected = false;
};