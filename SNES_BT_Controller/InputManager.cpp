#include "InputManager.h"
#include "SnesState.h"
void InputManager::begin() { buttons.begin(); accelerometer.begin(); joystick.begin(); }
bool InputManager::isSelectPressed() const { return buttons.isSelectPressed(); }
bool InputManager::isStartPressed() const { return buttons.isStartPressed(); }

void InputManager::printButtonTransitions() {
    const bool b = buttons.isBPressed();
    const bool y = buttons.isYPressed();
    const bool select = buttons.isSelectPressed();
    const bool start = buttons.isStartPressed();
    const bool a = buttons.isAPressed();
    const bool x = buttons.isXPressed();

    if (!debugInitialized) {
        lastB = b; lastY = y; lastSelect = select;
        lastStart = start; lastA = a; lastX = x;
        debugInitialized = true;
        Serial.println("[DEBUG] Monitoramento de botoes iniciado (eventos de pressionar/soltar).");
        return;
    }

    if (b != lastB) Serial.printf("[BUTTON] B %s\n", b ? "PRESSIONADO" : "SOLTO");
    if (a != lastA) Serial.printf("[BUTTON] A %s\n", a ? "PRESSIONADO" : "SOLTO");
    if (x != lastX) Serial.printf("[BUTTON] X %s\n", x ? "PRESSIONADO" : "SOLTO");
    if (y != lastY) Serial.printf("[BUTTON] Y %s\n", y ? "PRESSIONADO" : "SOLTO");
    if (select != lastSelect) Serial.printf("[BUTTON] SELECT %s\n", select ? "PRESSIONADO" : "SOLTO");
    if (start != lastStart) Serial.printf("[BUTTON] START %s\n", start ? "PRESSIONADO" : "SOLTO");

    lastB = b; lastY = y; lastSelect = select;
    lastStart = start; lastA = a; lastX = x;
}
void InputManager::setMode(InputMode mode) {
    currentMode=mode;
    Serial.printf("[INPUT] Modo: %s\n", currentMode==BUTTON_MODE ? "BUTTON" : "ACCELEROMETER");
}
uint8_t InputManager::readGripState() {
    // A garra vem de A/B nos dois modos; ambos ou nenhum = neutro.
    const bool open=buttons.isAPressed(), close=buttons.isBPressed();
    if (open && !close) return 1; // OPEN
    if (close && !open) return 2; // CLOSE
    return 0; // NEUTRAL
}
uint16_t InputManager::readState() {
    printButtonTransitions();
    // Botoes iguais nos dois modos: A/B (garra), X/Y (frente/tras), SELECT (modos) e START (calibracao).
    uint16_t state=Snes::ALL_RELEASED;
    if(buttons.isAPressed()) state &= ~(1u<<Snes::A);
    if(buttons.isBPressed()) state &= ~(1u<<Snes::B);
    if(buttons.isSelectPressed()) state &= ~(1u<<Snes::SELECT);
    if(buttons.isStartPressed()) state &= ~(1u<<Snes::START);
    if(buttons.isXPressed()) state &= ~(1u<<Snes::X); // frente
    if(buttons.isYPressed()) state &= ~(1u<<Snes::Y); // tras
    // Direcional: no modo acelerometro a inclinacao substitui o potenciometro.
    if (currentMode==ACCELEROMETER_MODE) return state & accelerometer.readState();
    return state & joystick.readState();
}

void InputManager::printDiagnostics() {
    if (currentMode==ACCELEROMETER_MODE) accelerometer.printDiagnostics();
    else joystick.printDiagnostics();
}
