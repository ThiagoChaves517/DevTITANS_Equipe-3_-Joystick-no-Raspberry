#include <Arduino.h>
#include "InputManager.h"
#include "SnesEngine.h"
#include "BleService.h"
#include "TransportManager.h"
#include "SnesState.h"
#include "FrameProtocol.h"
InputManager inputManager; SnesEngine snesEngine; BleService bleService; TransportManager transportManager;
void setup() {
    Serial.begin(115200); delay(500);
    Serial.println(); Serial.println("=============================="); Serial.println(" SNES-BT Controller"); Serial.println(" ADXL335 / INPUT MODES"); Serial.println("==============================");
    inputManager.begin(); snesEngine.begin(); bleService.begin(); transportManager.begin(&bleService);
    Serial.println("Inicializacao concluida. Mantenha o controle parado durante a calibracao inicial.");
}
void loop() {
    const bool selectPressed=inputManager.isSelectPressed();
    const bool startPressed=inputManager.isStartPressed();
    const uint16_t state=inputManager.readState();
    const uint8_t gripState=inputManager.readGripState();
    snesEngine.sendFrame(state);
    bleService.poll();
    switch(transportManager.update(state,selectPressed,startPressed,gripState)) {
    case TransportManager::TOGGLE_INPUT:
        inputManager.setMode(inputManager.mode()==InputManager::BUTTON_MODE ? InputManager::ACCELEROMETER_MODE : InputManager::BUTTON_MODE);
        break;
    case TransportManager::CALIBRATE_ACCEL: {
        // Avisa o receptor: P0=1 (calibrado), P1=maior variacao entre amostras (ADC, limitada a 255).
        const uint16_t spread=inputManager.calibrateAccelerometer();
        transportManager.sendFrame(Protocol::ACCEL_CALIBRATED, 1, spread>255 ? 255 : spread);
        break;
    }
    default:
        break;
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
        inputManager.printDiagnostics();
    }
    delay(8);
}
