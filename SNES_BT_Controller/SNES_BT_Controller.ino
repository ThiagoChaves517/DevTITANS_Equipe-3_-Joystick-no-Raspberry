#include <Arduino.h>
#include "InputManager.h"
#include "SnesEngine.h"
#include "BleService.h"
#include "TransportManager.h"
#include "SnesState.h"
InputManager inputManager; SnesEngine snesEngine; BleService bleService; TransportManager transportManager;
void setup() {
    Serial.begin(115200); delay(500);
    Serial.println(); Serial.println("=============================="); Serial.println(" SNES-BT Controller"); Serial.println(" ADXL335 / INPUT MODES"); Serial.println("==============================");
    inputManager.begin(); snesEngine.begin(); bleService.begin(); transportManager.begin(&bleService);
    Serial.println("Inicializacao concluida. Mantenha o controle parado durante a calibracao inicial.");
}
void loop() {
    const bool selectPressed=inputManager.isSelectPressed();
    const uint16_t state=inputManager.readState();
    const uint8_t gripState=inputManager.readGripState();
    snesEngine.sendFrame(state);
    bleService.poll();
    if(transportManager.update(state,selectPressed,gripState)) {
        inputManager.setMode(inputManager.mode()==InputManager::BUTTON_MODE ? InputManager::ACCELEROMETER_MODE : InputManager::BUTTON_MODE);
    }
    static uint32_t lastDiagnosticAt=0;
    const uint32_t now=millis();
    if (static_cast<uint32_t>(now-lastDiagnosticAt)>=500) {
        lastDiagnosticAt=now;
        Serial.printf("[STATUS] Entrada=%s Transporte=%s BLE=%s SELECT=%s\n",
            inputManager.mode()==InputManager::BUTTON_MODE ? "BUTTON" : "ACCELEROMETER",
            transportManager.mode()==TransportManager::BLUETOOTH ? "BLE" : "WIRED",
            bleService.isConnected() ? "CONECTADO" : "SEM_CLIENTE",
            selectPressed ? "PRESSIONADO" : "SOLTO");
        if (inputManager.mode()==InputManager::ACCELEROMETER_MODE) inputManager.printDiagnostics();
    }
    delay(8);
}
