#pragma once

#include <Arduino.h>
#include <BLEServer.h>
#include <BLECharacteristic.h>

class ServerCallbacks;

class BleService
{
public:
    void begin();

    void updateFrame(const uint8_t* frame, size_t length);

    bool isConnected() const;

    // Chamar a cada loop(): reinicia o advertising fora do callback da pilha BLE.
    void poll();

private:
    friend class ServerCallbacks;

    BLEServer* server = nullptr;
    BLECharacteristic* stateCharacteristic = nullptr;

    // Escritos na task do Bluetooth, lidos no loop().
    volatile bool connected = false;
    volatile bool restartAdvertising = false;
    volatile uint32_t disconnectedAt = 0;
};