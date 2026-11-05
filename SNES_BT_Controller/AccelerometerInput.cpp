#include "AccelerometerInput.h"
#include "BoardConfig.h"
#include "SnesState.h"
namespace {
constexpr float FILTER_ALPHA = 0.22f;
// ADXL335 em 3V3 + ADC 12 bits/11 dB: ~400 contagens por g. Inclinar ate 90 graus varia no maximo 1 g,
// entao o limiar precisa ficar bem abaixo disso para detectar inclinacao lenta (~200 = ~30 graus).
constexpr int TILT_PRESS_THRESHOLD = 200;   // delta ADC para pressionar; ajustar com dX/dY reais
constexpr int TILT_RELEASE_THRESHOLD = 140; // histerese: so solta abaixo deste valor
// Modulo montado em pe, de frente para o jogador: o eixo X do sensor aponta para baixo (gravidade) e quase nao
// muda. Girar como volante move a gravidade para o eixo Y; inclinar o topo para frente/tras move para o eixo Z.
constexpr bool INVERT_LEFT_RIGHT = false; // trocar se esquerda/direita sairem invertidos
constexpr bool INVERT_UP_DOWN = false;    // trocar se cima/baixo sairem invertidos
constexpr uint8_t CALIBRATION_SAMPLES = 32;
}
void AccelerometerInput::begin() {
    analogReadResolution(12);
    analogSetPinAttenuation(Board::ACCEL_X, ADC_11db);
    analogSetPinAttenuation(Board::ACCEL_Y, ADC_11db);
    analogSetPinAttenuation(Board::ACCEL_Z, ADC_11db);
    calibrate();
}
uint16_t AccelerometerInput::calibrate() {
    uint32_t sx=0, sy=0, sz=0;
    uint16_t minX=0xFFFF, minY=0xFFFF, minZ=0xFFFF, maxX=0, maxY=0, maxZ=0;
    for (uint8_t i=0;i<CALIBRATION_SAMPLES;++i) {
        const uint16_t x=readAverage(Board::ACCEL_X), y=readAverage(Board::ACCEL_Y), z=readAverage(Board::ACCEL_Z);
        sx += x; sy += y; sz += z;
        minX=min(minX,x); maxX=max(maxX,x);
        minY=min(minY,y); maxY=max(maxY,y);
        minZ=min(minZ,z); maxZ=max(maxZ,z);
        delay(2); // ~70 ms no total: abaixo do watchdog de 100 ms do receptor
    }
    neutralX = filteredX = sx / float(CALIBRATION_SAMPLES);
    neutralY = filteredY = sy / float(CALIBRATION_SAMPLES);
    neutralZ = filteredZ = sz / float(CALIBRATION_SAMPLES);
    lastUp = lastDown = lastLeft = lastRight = false;
    calibrated = true;
    const uint16_t spreadX=maxX-minX, spreadY=maxY-minY, spreadZ=maxZ-minZ;
    Serial.printf("[ACCEL] Calibrado X=%.1f Y=%.1f Z=%.1f | variacao X=%u Y=%u Z=%u\n",
        neutralX, neutralY, neutralZ, spreadX, spreadY, spreadZ);
    // Parado, cada eixo varia poucas dezenas de contagens. Variacao alta = movimento ou pino solto/errado.
    if (spreadX>TILT_RELEASE_THRESHOLD || spreadY>TILT_RELEASE_THRESHOLD || spreadZ>TILT_RELEASE_THRESHOLD)
        Serial.println("[ACCEL] AVISO: variacao alta na calibracao; mantenha o controle parado e confira a fiacao dos eixos");
    return max(spreadX, max(spreadY, spreadZ));
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

    const float dy = filteredY - neutralY;
    const float dz = filteredZ - neutralZ;

    // O acelerometro substitui so o direcional: girar como volante -> LEFT/RIGHT (eixo Y do sensor),
    // inclinar o topo para o jogador/para frente -> UP/DOWN (eixo Z).
    const float sideways = INVERT_LEFT_RIGHT ? -dy : dy;
    const float vertical = INVERT_UP_DOWN ? -dz : dz;
    auto tilted = [](float delta, bool wasPressed) {
        return delta > (wasPressed ? TILT_RELEASE_THRESHOLD : TILT_PRESS_THRESHOLD);
    };
    const bool up    = tilted( vertical, lastUp);
    const bool down  = tilted(-vertical, lastDown);
    const bool right = tilted( sideways, lastRight);
    const bool left  = tilted(-sideways, lastLeft);

    // Os bits SNES sao ativos em LOW: limpar o bit significa pressionado.
    uint16_t state = Snes::ALL_RELEASED;
    if (up)    state &= ~(1u << Snes::UP);
    if (down)  state &= ~(1u << Snes::DOWN);
    if (left)  state &= ~(1u << Snes::LEFT);
    if (right) state &= ~(1u << Snes::RIGHT);

    // Debug por transicao: uma mensagem ao pressionar e outra ao soltar.
    if (up != lastUp)       Serial.printf("[ACCEL] CIMA %s (dZ=%+.0f)\n", up ? "PRESSIONADO" : "SOLTO", dz);
    if (down != lastDown)   Serial.printf("[ACCEL] BAIXO %s (dZ=%+.0f)\n", down ? "PRESSIONADO" : "SOLTO", dz);
    if (right != lastRight) Serial.printf("[ACCEL] DIREITA %s (dY=%+.0f)\n", right ? "PRESSIONADO" : "SOLTO", dy);
    if (left != lastLeft)   Serial.printf("[ACCEL] ESQUERDA %s (dY=%+.0f)\n", left ? "PRESSIONADO" : "SOLTO", dy);

    lastUp = up; lastDown = down; lastLeft = left; lastRight = right;
    return state;
}

void AccelerometerInput::printDiagnostics() {
    const float dx=filteredX-neutralX, dy=filteredY-neutralY, dz=filteredZ-neutralZ;
    Serial.printf("[ACCEL] X=%.0f Y=%.0f Z=%.0f | dX=%+.0f dY=%+.0f dZ=%+.0f\n",
        filteredX,filteredY,filteredZ,dx,dy,dz);
}
