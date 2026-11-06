#include "forwarder.h"

#include "FrameProtocol.h"

namespace snes_arm {

void Forwarder::publish(uint16_t state) {
    state = uint16_t(state | 0xF000);  // bits 12-15 não são botões (sempre 1, como no firmware)
    if (state == state_) return;
    state_ = state;
    pending_ = true;
}

bool Forwarder::send_now(long long now_ms) {
    uint8_t frame[Protocol::FRAME_SIZE];
    Protocol::encode(Protocol::CONTROLLER_STATE, seq_, uint8_t(state_), uint8_t(state_ >> 8), frame);
    if (!sink_.send(frame, sizeof(frame))) {
        ++send_errors_;
        return false;  // continua pendente: tenta de novo quando o link voltar
    }
    ++seq_;
    ++frames_sent_;
    pending_ = false;
    last_send_ms_ = now_ms;
    return true;
}

void Forwarder::tick(long long now_ms) {
    if (!sink_.is_ready()) return;
    if (pending_ || (!is_neutral() && now_ms - last_send_ms_ >= keepalive_ms_)) send_now(now_ms);
}

long Forwarder::next_wakeup_ms(long long now_ms) const {
    if (!sink_.is_ready()) return -1;  // sem link não há o que enviar: quem dita a espera é o link
    if (pending_) return 0;
    if (is_neutral()) return -1;
    const long long left = last_send_ms_ + keepalive_ms_ - now_ms;
    return left > 0 ? long(left) : 0;
}

}  // namespace snes_arm
