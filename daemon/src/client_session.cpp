#include "client_session.h"

#include <algorithm>
#include <cstring>

namespace snes {

bool ClientSession::consume(const uint8_t* data, size_t len, std::vector<uint8_t>& reply, std::string& reason) {
    while (len > 0) {
        const size_t take = std::min(len, ipc::kMessageSize - fill_);
        std::memcpy(buf_ + fill_, data, take);
        fill_ += take;
        data += take;
        len -= take;
        if (fill_ < ipc::kMessageSize) break;
        fill_ = 0;
        ipc::Message m;
        const ipc::DecodeError err = ipc::decode(buf_, m);
        if (err != ipc::DecodeError::NONE) {
            reason = std::string("mensagem inválida: ") + ipc::to_string(err);
            return false;
        }
        if (!handle(m, reply, reason)) return false;
    }
    return true;
}

bool ClientSession::handle(const ipc::Message& m, std::vector<uint8_t>& reply, std::string& reason) {
    ++messages_;
    last_seq_ = m.seq;
    if (m.type == ipc::HELLO) {
        handshaken_ = true;
        ipc::Message ack;
        ack.type = ipc::HELLO_ACK;
        ack.seq = m.seq;
        ack.status = ipc::STATUS_OK;
        uint8_t out[ipc::kMessageSize];
        ipc::encode(ack, out);
        reply.insert(reply.end(), out, out + ipc::kMessageSize);
        return true;
    }
    if (!handshaken_) {
        reason = "mensagem de estado antes do HELLO";
        return false;
    }
    if (m.type == ipc::NEUTRAL) device_.neutralize();
    else device_.publish(m.buttons, m.grip);
    return true;
}

}  // namespace snes
