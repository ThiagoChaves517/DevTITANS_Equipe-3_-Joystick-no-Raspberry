#include "snes_register.h"

namespace snes_arm {
namespace {
uint16_t set_pressed(uint16_t state, int bit, bool pressed) {
    return pressed ? uint16_t(state & ~(1u << bit)) : uint16_t(state | (1u << bit));
}

// value < 0 aperta "negative", value > 0 aperta "positive", 0 solta os dois.
uint16_t with_axis(uint16_t state, int negative, int positive, int value) {
    state = set_pressed(state, negative, value < 0);
    return set_pressed(state, positive, value > 0);
}
}  // namespace

uint16_t SnesRegister::with_hat_x(uint16_t state, int value) { return with_axis(state, kBitLeft, kBitRight, value); }
uint16_t SnesRegister::with_hat_y(uint16_t state, int value) { return with_axis(state, kBitUp, kBitDown, value); }

bool SnesRegister::apply(int type, int c, int value) {
    uint16_t next = state_;

    if (type == code::EV_KEY) {
        for (const KeyBit& k : kKeyBits) {
            if (k.code == c) {
                next = set_pressed(state_, k.bit, value != 0);  // value 2 = autorepeat: continua pressionado
                break;
            }
        }
    } else if (type == code::EV_ABS && c == code::ABS_HAT0X) {
        next = with_hat_x(state_, value);
    } else if (type == code::EV_ABS && c == code::ABS_HAT0Y) {
        next = with_hat_y(state_, value);
    }

    if (next == state_) return false;
    state_ = next;
    return true;
}

}  // namespace snes_arm
