// Estado publicado do controle e mapeamento SNES -> eventos Linux Input.
#pragma once
#include <cstdint>

namespace snes {

// Códigos Linux usados (linux/input-event-codes.h), replicados para compilar os testes fora do Linux.
namespace code {
constexpr int EV_SYN = 0x00, EV_KEY = 0x01, EV_ABS = 0x03;
constexpr int BTN_A = 0x130, BTN_B = 0x131, BTN_X = 0x133, BTN_Y = 0x134, BTN_TL = 0x136, BTN_TR = 0x137;
constexpr int BTN_SELECT = 0x13a, BTN_START = 0x13b;
constexpr int BTN_0 = 0x100, BTN_1 = 0x101;
constexpr int ABS_HAT0X = 0x10, ABS_HAT0Y = 0x11;
}  // namespace code

// Destino dos eventos (uinput em produção, fake nos testes).
class InputSink {
public:
    virtual ~InputSink() = default;
    virtual bool emit(int type, int code, int value) = 0;
    virtual bool sync() = 0;
};

struct ButtonMap {
    int bit;
    int code;
    const char* name;
};

// Bits do firmware (SnesState.h) -> botões de gamepad. Mapeamento por nome (SNES A -> BTN_A).
constexpr ButtonMap kButtons[] = {
    {0, code::BTN_B, "B"},          {1, code::BTN_Y, "Y"},          {2, code::BTN_SELECT, "SELECT"},
    {3, code::BTN_START, "START"},  {8, code::BTN_A, "A"},          {9, code::BTN_X, "X"},
    {10, code::BTN_TL, "L"},        {11, code::BTN_TR, "R"},
};
constexpr int kBitUp = 4, kBitDown = 5, kBitLeft = 6, kBitRight = 7;
// GRIP_STATE: 1 = abrir -> BTN_0, 2 = fechar -> BTN_1. O Generic.kl do Android só mapeia BUTTON_1..16 a
// partir de BTN_0 (BTN_TRIGGER_HAPPY* chega como keyCode UNKNOWN; verificado no GloDroid).
constexpr int kGripOpenCode = code::BTN_0, kGripCloseCode = code::BTN_1;
constexpr int kHatXCode = code::ABS_HAT0X, kHatYCode = code::ABS_HAT0Y;

class ControllerDevice {
public:
    explicit ControllerDevice(InputSink& sink) : sink_(sink) {}

    // Publica um estado completo; emite apenas as transições. Retorna true se algo mudou.
    bool publish(uint16_t buttons, uint8_t grip);

    // Libera tudo. Retorna false (sem eventos) se já estiver neutro.
    bool neutralize() { return publish(0xFFFF, 0); }

    bool is_neutral() const { return buttons_ == 0xFFFF && grip_ == 0; }
    uint16_t buttons() const { return buttons_; }
    uint8_t grip() const { return grip_; }
    unsigned long write_errors() const { return write_errors_; }

    static int hat_x(uint16_t buttons);
    static int hat_y(uint16_t buttons);

private:
    void emit(int type, int code, int value);

    InputSink& sink_;
    uint16_t buttons_ = 0xFFFF;
    uint8_t grip_ = 0;
    unsigned long write_errors_ = 0;
};

}  // namespace snes
