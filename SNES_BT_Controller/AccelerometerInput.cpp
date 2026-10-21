#include "AccelerometerInput.h"
#include "BoardConfig.h"
#include "SnesState.h"
namespace {
constexpr float FILTER_ALPHA = 0.22f;
constexpr int TILT_THRESHOLD = 520; // delta ADC; ajustar apos teste real
constexpr int GRIP_THRESHOLD = 650;
constexpr uint8_t CALIBRATION_SAMPLES = 32;
}
void AccelerometerInput::begin() {
    analogReadResolution(12);
    analogSetPinAttenuation(Board::ACCEL_X, ADC_11db);
    analogSetPinAttenuation(Board::ACCEL_Y, ADC_11db);
    analogSetPinAttenuation(Board::ACCEL_Z, ADC_11db);
    uint32_t sx=0, sy=0, sz=0;
    for (uint8_t i=0;i<CALIBRATION_SAMPLES;++i) {
        sx += readAverage(Board::ACCEL_X); sy += readAverage(Board::ACCEL_Y); sz += readAverage(Board::ACCEL_Z);
        delay(4);
    }
    neutralX = filteredX = sx / float(CALIBRATION_SAMPLES);
    neutralY = filteredY = sy / float(CALIBRATION_SAMPLES);
    neutralZ = filteredZ = sz / float(CALIBRATION_SAMPLES);
    calibrated = true;
    Serial.printf("[ACCEL] Calibrado X=%.1f Y=%.1f Z=%.1f. Mantenha o controle parado na inicializacao.\n", neutralX, neutralY, neutralZ);
}
uint16_t AccelerometerInput::readAverage(uint8_t pin) {
    uint32_t sum=0; for (uint8_t i=0;i<4;++i) sum += analogRead(pin);
    return static_cast<uint16_t>(sum/4);
}
uint16_t AccelerometerInput::readState() {
    if (!calibrated) return Snes::ALL_RELEASED;

    const float x = readAverage(Board::ACCEL_X);
    const float y = readAverage(Board::ACCEL_Y);
    const float z = readAverage(Board::ACCEL_Z);
    filteredX += FILTER_ALPHA * (x - filteredX);
    filteredY += FILTER_ALPHA * (y - filteredY);
    filteredZ += FILTER_ALPHA * (z - filteredZ);

    const float dx = filteredX - neutralX;
    const float dy = filteredY - neutralY;
    uint16_t state = Snes::ALL_RELEASED;

    // Mapeamento solicitado pelo usuario:
    // levantar -> X, abaixar -> B, direita -> A, esquerda -> Y.
    // A inclinacao para cima/baixo usa o eixo Y; esquerda/direita usa X.
    const bool mappedX = dy < -TILT_THRESHOLD;
    const bool mappedB = dy >  TILT_THRESHOLD;
    const bool mappedY = dx < -TILT_THRESHOLD;
    const bool mappedA = dx >  TILT_THRESHOLD;

    // Os bits SNES sao ativos em LOW: limpar o bit significa pressionado.
    if (mappedX) state &= ~(1u << Snes::X);
    if (mappedB) state &= ~(1u << Snes::B);
    if (mappedA) state &= ~(1u << Snes::A);
    if (mappedY) state &= ~(1u << Snes::Y);

    // Debug por transicao: uma mensagem ao pressionar e outra ao soltar.
    if (mappedX != lastMappedX) Serial.printf("[ACCEL->BUTTON] X %s (dY=%+.0f)\n", mappedX ? "PRESSIONADO" : "SOLTO", dy);
    if (mappedB != lastMappedB) Serial.printf("[ACCEL->BUTTON] B %s (dY=%+.0f)\n", mappedB ? "PRESSIONADO" : "SOLTO", dy);
    if (mappedA != lastMappedA) Serial.printf("[ACCEL->BUTTON] A %s (dX=%+.0f)\n", mappedA ? "PRESSIONADO" : "SOLTO", dx);
    if (mappedY != lastMappedY) Serial.printf("[ACCEL->BUTTON] Y %s (dX=%+.0f)\n", mappedY ? "PRESSIONADO" : "SOLTO", dx);

    lastMappedX = mappedX;
    lastMappedB = mappedB;
    lastMappedA = mappedA;
    lastMappedY = mappedY;
    return state;
}

uint8_t AccelerometerInput::readGripState() {
    if (!calibrated) return 0;
    float z=readAverage(Board::ACCEL_Z); filteredZ += FILTER_ALPHA*(z-filteredZ);
    float dz=filteredZ-neutralZ;
    if (dz > GRIP_THRESHOLD) return 1; // OPEN
    if (dz < -GRIP_THRESHOLD) return 2; // CLOSE
    return 0; // NEUTRAL
}

void AccelerometerInput::printDiagnostics() {
    const float dx=filteredX-neutralX, dy=filteredY-neutralY, dz=filteredZ-neutralZ;
    Serial.printf("[ACCEL] X=%.0f Y=%.0f Z=%.0f | dX=%+.0f dY=%+.0f dZ=%+.0f\n",
        filteredX,filteredY,filteredZ,dx,dy,dz);
}
