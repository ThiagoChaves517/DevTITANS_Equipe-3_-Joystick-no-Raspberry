#include "JoystickInput.h"
#include "BoardConfig.h"
#include "SnesState.h"
namespace {
// Potenciometro de 0 a 3,3 V no ADC de 12 bits: curso ~0-4095, centro ~2048.
constexpr int PRESS_THRESHOLD = 700;   // ~1/3 do curso a partir do centro
constexpr int RELEASE_THRESHOLD = 550; // histerese: so solta abaixo deste valor
constexpr bool INVERT_X = false; // trocar se esquerda/direita sairem invertidos
constexpr bool INVERT_Y = true;  // trocar se cima/baixo sairem invertidos (no hardware atual saem)
constexpr uint8_t CALIBRATION_SAMPLES = 16;
uint16_t readAverage(uint8_t pin) {
    uint32_t sum=0; for (uint8_t i=0;i<4;++i) sum += analogRead(pin);
    return static_cast<uint16_t>(sum/4);
}
}
void JoystickInput::begin() {
    analogReadResolution(12);
    analogSetPinAttenuation(Board::JOY_X, ADC_11db);
    analogSetPinAttenuation(Board::JOY_Y, ADC_11db);
    calibrate();
}
void JoystickInput::calibrate() {
    uint32_t sx=0, sy=0;
    for (uint8_t i=0;i<CALIBRATION_SAMPLES;++i) {
        sx += readAverage(Board::JOY_X); sy += readAverage(Board::JOY_Y);
        delay(2);
    }
    centerX = sx / float(CALIBRATION_SAMPLES);
    centerY = sy / float(CALIBRATION_SAMPLES);
    lastUp = lastDown = lastLeft = lastRight = false;
    calibrated = true;
    Serial.printf("[JOY] Centro X=%.0f Y=%.0f. Mantenha o direcional solto na inicializacao.\n", centerX, centerY);
}
uint16_t JoystickInput::readState() {
    if (!calibrated) return Snes::ALL_RELEASED;

    rawX = readAverage(Board::JOY_X);
    rawY = readAverage(Board::JOY_Y);
    float dx = rawX - centerX;
    float dy = rawY - centerY;
    if (INVERT_X) dx = -dx;
    if (INVERT_Y) dy = -dy;

    auto pushed = [](float delta, bool wasPressed) {
        return delta > (wasPressed ? RELEASE_THRESHOLD : PRESS_THRESHOLD);
    };
    const bool right = pushed( dx, lastRight);
    const bool left  = pushed(-dx, lastLeft);
    const bool up    = pushed( dy, lastUp);
    const bool down  = pushed(-dy, lastDown);

    uint16_t state = Snes::ALL_RELEASED;
    if (up)    state &= ~(1u << Snes::UP);
    if (down)  state &= ~(1u << Snes::DOWN);
    if (left)  state &= ~(1u << Snes::LEFT);
    if (right) state &= ~(1u << Snes::RIGHT);

    if (up != lastUp)       Serial.printf("[JOY] CIMA %s (dY=%+.0f)\n", up ? "PRESSIONADO" : "SOLTO", dy);
    if (down != lastDown)   Serial.printf("[JOY] BAIXO %s (dY=%+.0f)\n", down ? "PRESSIONADO" : "SOLTO", dy);
    if (left != lastLeft)   Serial.printf("[JOY] ESQUERDA %s (dX=%+.0f)\n", left ? "PRESSIONADO" : "SOLTO", dx);
    if (right != lastRight) Serial.printf("[JOY] DIREITA %s (dX=%+.0f)\n", right ? "PRESSIONADO" : "SOLTO", dx);

    lastUp = up; lastDown = down; lastLeft = left; lastRight = right;
    return state;
}
void JoystickInput::printDiagnostics() {
    Serial.printf("[JOY] X=%u Y=%u | dX=%+.0f dY=%+.0f\n", rawX, rawY, rawX-centerX, rawY-centerY);
}
