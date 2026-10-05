#include "BleService.h"

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLE2902.h>

namespace
{

    constexpr const char* DEVICE_NAME =
        "SNES-BT-Controller";

    constexpr const char* SERVICE_UUID =
        "8b7e0001-5a9d-4c31-9f20-7d6a8e42b101";

    constexpr const char* STATE_CHARACTERISTIC_UUID =
        "8b7e0002-5a9d-4c31-9f20-7d6a8e42b101";
}

class ServerCallbacks : public BLEServerCallbacks
{
public:

    explicit ServerCallbacks(BleService* owner)
        : owner(owner)
    {
    }

    void onConnect(BLEServer* server) override
    {
        owner->connected = true;

        Serial.println();
        Serial.println("[BLE] Cliente conectado.");
    }

    void onDisconnect(BLEServer* server) override
    {
        owner->connected = false;

        Serial.println();
        Serial.println("[BLE] Cliente desconectado.");

        server->startAdvertising();

        Serial.println("[BLE] Advertising reiniciado.");
    }

private:

    BleService* owner;
};

void BleService::begin()
{
    Serial.println();
    Serial.println("==============================");
    Serial.println(" BLE");
    Serial.println(" Etapa 04");
    Serial.println("==============================");

    if (!BLEDevice::init(DEVICE_NAME))
    {
        Serial.println("[BLE] ERRO: BLEDevice::init()");
        return;
    }

    Serial.printf(
        "[BLE] Device: %s\n",
        DEVICE_NAME
    );

    server = BLEDevice::createServer();

    if (server == nullptr)
    {
        Serial.println("[BLE] ERRO: createServer()");
        return;
    }

    server->setCallbacks(
        new ServerCallbacks(this)
    );

    BLEService* service =
        server->createService(SERVICE_UUID);

    if (service == nullptr)
    {
        Serial.println("[BLE] ERRO: createService()");
        return;
    }

    stateCharacteristic =
        service->createCharacteristic(
            STATE_CHARACTERISTIC_UUID,
            BLECharacteristic::PROPERTY_READ |
            BLECharacteristic::PROPERTY_NOTIFY
        );

    if (stateCharacteristic == nullptr)
    {
        Serial.println(
            "[BLE] ERRO: createCharacteristic()"
        );

        return;
    }

    stateCharacteristic->addDescriptor(
        new BLE2902()
    );

    const uint16_t initialState = 0xFFFF;

    uint8_t data[2];

    data[0] = initialState & 0xFF;
    data[1] = (initialState >> 8) & 0xFF;

    stateCharacteristic->setValue(
        data,
        sizeof(data)
    );

    service->start();

    BLEAdvertising* advertising =
        BLEDevice::getAdvertising();

    advertising->addServiceUUID(
        SERVICE_UUID
    );

    advertising->setScanResponse(true);

    advertising->setMinPreferred(0x06);
    advertising->setMaxPreferred(0x12);

    BLEDevice::startAdvertising();

    Serial.println("[BLE] Service iniciado.");
    Serial.println("[BLE] Advertising iniciado.");

    Serial.printf(
        "[BLE] Service UUID: %s\n",
        SERVICE_UUID
    );

    Serial.printf(
        "[BLE] State UUID: %s\n",
        STATE_CHARACTERISTIC_UUID
    );
}

void BleService::update(uint16_t state)
{

    if (!connected)
        return;

    if (stateCharacteristic == nullptr)
        return;

    uint8_t data[2];

    data[0] = state & 0xFF;
    data[1] = (state >> 8) & 0xFF;

    stateCharacteristic->setValue(
        data,
        sizeof(data)
    );

    stateCharacteristic->notify();
}

bool BleService::isConnected() const
{
    return connected;
}