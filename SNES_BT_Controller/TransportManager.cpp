#include "TransportManager.h"
#include "FrameProtocol.h"
namespace { constexpr uint32_t LONG_PRESS_MS=5000, DEBOUNCE_MS=40; }
void TransportManager::begin(BleService* service) {
    ble=service;
    uart.begin(115200);
    Serial.println("[TRANSPORT] Modo inicial: BLUETOOTH");
}
bool TransportManager::update(uint16_t state, bool selectPressed, uint8_t gripState) {
    const uint32_t now=millis();
    bool inputModeToggle=false;
    // Debounce real: apenas uma entrada que permanece estavel por DEBOUNCE_MS altera o estado logico.
    if (selectPressed != rawSelect) {
        rawSelect=selectPressed;
        rawChangedAt=now;
    }
    if (rawSelect != stableSelect && static_cast<uint32_t>(now-rawChangedAt)>=DEBOUNCE_MS) {
        stableSelect=rawSelect;
        if (stableSelect) {
            selectPressedAt=now;
            longPressHandled=false;
            Serial.println("[SELECT] Pressionado; mantenha por 5 segundos para alternar entrada");
        } else {
            const uint32_t elapsed=now-selectPressedAt;
            if (!longPressHandled && elapsed>=LONG_PRESS_MS) {
                longPressHandled=true;
                inputModeToggle=true;
                Serial.println("[SELECT] Pressao longa reconhecida na soltura");
            } else if (!longPressHandled) {
                Serial.printf("[SELECT] Pressao curta (%lu ms): trocar transporte\n",static_cast<unsigned long>(elapsed));
                switchTransport();
            } else {
                Serial.println("[SELECT] Solto apos pressao longa; transporte mantido");
            }
        }
    }
    if (stableSelect && !longPressHandled && static_cast<uint32_t>(now-selectPressedAt)>=LONG_PRESS_MS) {
        longPressHandled=true;
        inputModeToggle=true;
        Serial.println("[SELECT] 5 segundos: trocar BUTTON/ACCELEROMETER (uma vez)");
    }
    sendFrame(Protocol::CONTROLLER_STATE, state & 0xFF, (state>>8)&0xFF);
    sendFrame(Protocol::GRIP_STATE, gripState, 0);
    return inputModeToggle;
}
void TransportManager::switchTransport() {
    currentMode=currentMode==BLUETOOTH?WIRED:BLUETOOTH;
    Serial.printf("[TRANSPORT] Alternado localmente para %s\n",currentMode==BLUETOOTH?"BLUETOOTH":"WIRED");
}
void TransportManager::sendFrame(uint8_t type,uint8_t p0,uint8_t p1) {
    uint8_t frame[Protocol::FRAME_SIZE];
    Protocol::encode(type,sequence++,p0,p1,frame);
    if(currentMode==BLUETOOTH) { if(ble) ble->updateFrame(frame,sizeof(frame)); }
    else uart.sendFrame(frame,sizeof(frame));
}
