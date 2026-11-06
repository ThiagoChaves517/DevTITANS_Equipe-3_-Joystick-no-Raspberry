// Eventos Linux Input do gamepad "SNES-BT-Controller" -> registrador SNES de 16 bits.
//
// É o caminho inverso do ControllerDevice do snes_input_daemon (branch driver): ele
// transforma os 16 bits em eventos; aqui os eventos voltam a ser os 16 bits, para
// o braço receber exatamente o que o controle enviou.
#pragma once
#include <cstdint>

namespace snes_arm {

// Códigos Linux (linux/input-event-codes.h), replicados para compilar os testes fora do Linux.
namespace code {
constexpr int EV_SYN = 0x00, EV_KEY = 0x01, EV_ABS = 0x03;
constexpr int SYN_REPORT = 0, SYN_DROPPED = 3;
constexpr int BTN_A = 0x130, BTN_B = 0x131, BTN_X = 0x133, BTN_Y = 0x134, BTN_TL = 0x136, BTN_TR = 0x137;
constexpr int BTN_SELECT = 0x13a, BTN_START = 0x13b;
constexpr int BTN_0 = 0x100, BTN_1 = 0x101;
constexpr int ABS_HAT0X = 0x10, ABS_HAT0Y = 0x11;
}  // namespace code

struct KeyBit {
    int code;
    int bit;
};

// Mesma tabela do snes_input_daemon (controller_device.h, kButtons), no sentido inverso.
constexpr KeyBit kKeyBits[] = {
    {code::BTN_B, 0},  {code::BTN_Y, 1},  {code::BTN_SELECT, 2}, {code::BTN_START, 3},
    {code::BTN_A, 8},  {code::BTN_X, 9},  {code::BTN_TL, 10},    {code::BTN_TR, 11},
};
constexpr int kBitUp = 4, kBitDown = 5, kBitLeft = 6, kBitRight = 7;
// Para conferir contra o kernel sem citar snes::code depois de incluir linux/input.h (macros colidem).
constexpr int kHatXCode = code::ABS_HAT0X, kHatYCode = code::ABS_HAT0Y, kSynDroppedCode = code::SYN_DROPPED;
constexpr uint16_t kAllReleased = 0xFFFF;

class SnesRegister {
public:
    // Aplica um evento. Retorna true se o registrador mudou.
    // BTN_0/BTN_1 (garra pelo acelerômetro, GRIP_STATE) não fazem parte dos 16 bits: ignorados.
    bool apply(int type, int event_code, int value);

    // Estado completo lido do kernel (abertura do dispositivo e SYN_DROPPED).
    void set(uint16_t state) { state_ = uint16_t(state | 0xF000); }
    void reset() { state_ = kAllReleased; }

    uint16_t value() const { return state_; }
    bool is_neutral() const { return state_ == kAllReleased; }

    // Bits do D-pad a partir de ABS_HAT0X/Y (-1, 0, +1).
    static uint16_t with_hat_x(uint16_t state, int value);
    static uint16_t with_hat_y(uint16_t state, int value);

private:
    uint16_t state_ = kAllReleased;
};

}  // namespace snes_arm
