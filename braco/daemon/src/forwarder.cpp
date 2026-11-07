#include "forwarder.h"

#include <cstdio>

#include "FrameProtocol.h"

namespace snes_arm {

size_t encode_william(uint16_t state, char* out, size_t len) {
    const unsigned pressed = unsigned(~state) & 0x0FFFu;  // bit 1 = pressionado, só os 12 botões
    const int n = std::snprintf(out, len, "%u\n", pressed);
    return n > 0 && size_t(n) < len ? size_t(n) : 0;
}

void Forwarder::publish(uint16_t state) {
    state = uint16_t(state | 0xF000);  // bits 12-15 não são botões (sempre 1, como no firmware)
    if (state == state_) return;
    state_ = state;
    pending_ = true;
}

bool Forwarder::send_now(long long now_ms) {
    bool ok;
    if (wire_ == Wire::William) {
        char line[8];
        const size_t n = encode_william(state_, line, sizeof(line));
        ok = sink_.send(reinterpret_cast<const uint8_t*>(line), n);
    } else {
        uint8_t frame[Protocol::FRAME_SIZE];
        Protocol::encode(Protocol::CONTROLLER_STATE, seq_, uint8_t(state_), uint8_t(state_ >> 8), frame);
        ok = sink_.send(frame, sizeof(frame));
    }
    if (!ok) {
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
    if (wire_ == Wire::William) {
        // Cada linha = 1° no braço. Solto: nada no fio. Pressionado: uma linha a cada keepalive_ms, no máximo.
        if (is_neutral()) {
            pending_ = false;
            return;
        }
        if (now_ms - last_send_ms_ >= keepalive_ms_) send_now(now_ms);
        return;
    }
    if (pending_ || (!is_neutral() && now_ms - last_send_ms_ >= keepalive_ms_)) send_now(now_ms);
}

long Forwarder::next_wakeup_ms(long long now_ms) const {
    if (!sink_.is_ready()) return -1;  // sem link não há o que enviar: quem dita a espera é o link
    if (wire_ == Wire::Frame && pending_) return 0;
    if (is_neutral()) return -1;
    const long long left = last_send_ms_ + keepalive_ms_ - now_ms;
    return left > 0 ? long(left) : 0;
}

}  // namespace snes_arm
