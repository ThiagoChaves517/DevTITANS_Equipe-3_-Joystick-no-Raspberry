// Processa o stream de um cliente IPC: reenquadramento, handshake e validação.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "controller_device.h"
#include "ipc_protocol.h"

namespace snes {

class ClientSession {
public:
    explicit ClientSession(ControllerDevice& device) : device_(device) {}

    // Consome bytes recebidos. Respostas (HELLO_ACK) são anexadas a `reply`.
    // Retorna false se a conexão deve ser encerrada; o motivo fica em `reason`.
    // Mensagens válidas anteriores ao erro já foram aplicadas.
    bool consume(const uint8_t* data, size_t len, std::vector<uint8_t>& reply, std::string& reason);

    bool handshaken() const { return handshaken_; }
    size_t partial_bytes() const { return fill_; }
    unsigned long messages() const { return messages_; }
    uint16_t last_seq() const { return last_seq_; }

private:
    bool handle(const ipc::Message& m, std::vector<uint8_t>& reply, std::string& reason);

    ControllerDevice& device_;
    uint8_t buf_[ipc::kMessageSize] = {};
    size_t fill_ = 0;
    bool handshaken_ = false;
    unsigned long messages_ = 0;
    uint16_t last_seq_ = 0;
};

}  // namespace snes
