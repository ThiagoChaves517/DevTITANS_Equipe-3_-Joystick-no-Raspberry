#include "controller_device.h"

namespace snes {
namespace {
bool pressed(uint16_t buttons, int bit) { return (buttons & (1u << bit)) == 0; }

int axis(bool negative, bool positive) {
    if (negative == positive) return 0;  // nenhum ou ambos (opostos se anulam)
    return negative ? -1 : 1;
}
}  // namespace

int ControllerDevice::hat_x(uint16_t b) { return axis(pressed(b, kBitLeft), pressed(b, kBitRight)); }
int ControllerDevice::hat_y(uint16_t b) { return axis(pressed(b, kBitUp), pressed(b, kBitDown)); }

void ControllerDevice::emit(int type, int c, int value) {
    if (!sink_.emit(type, c, value)) ++write_errors_;
}

bool ControllerDevice::publish(uint16_t buttons, uint8_t grip) {
    buttons |= 0xF000;  // bits sem botão SNES
    bool changed = false;
    for (const ButtonMap& m : kButtons) {
        const bool was = pressed(buttons_, m.bit), now = pressed(buttons, m.bit);
        if (was != now) { emit(code::EV_KEY, m.code, now ? 1 : 0); changed = true; }
    }
    if (hat_x(buttons) != hat_x(buttons_)) { emit(code::EV_ABS, code::ABS_HAT0X, hat_x(buttons)); changed = true; }
    if (hat_y(buttons) != hat_y(buttons_)) { emit(code::EV_ABS, code::ABS_HAT0Y, hat_y(buttons)); changed = true; }
    const bool wasOpen = grip_ == 1, nowOpen = grip == 1, wasClose = grip_ == 2, nowClose = grip == 2;
    if (wasOpen != nowOpen) { emit(code::EV_KEY, kGripOpenCode, nowOpen ? 1 : 0); changed = true; }
    if (wasClose != nowClose) { emit(code::EV_KEY, kGripCloseCode, nowClose ? 1 : 0); changed = true; }
    buttons_ = buttons;
    grip_ = grip;
    if (changed && !sink_.sync()) ++write_errors_;
    return changed;
}

}  // namespace snes
