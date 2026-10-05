#include <Arduino.h>

#include "InputManager.h"
#include "SnesEngine.h"
#include "BleService.h"

InputManager inputManager;
SnesEngine snesEngine;
BleService bleService;

void setup()
{
    Serial.begin(115200);

    delay(500);

    Serial.println();
    Serial.println("==============================");
    Serial.println(" SNES-BT Controller");
    Serial.println(" Etapa 04 - BLE");
    Serial.println("==============================");

    inputManager.begin();

    snesEngine.begin();

    bleService.begin();

    Serial.println();
    Serial.println("Inicializacao concluida.");
}

void loop()
{

    const uint16_t state =
        inputManager.readState();

    snesEngine.sendFrame(state);

    bleService.update(state);

    delay(8);
}