// Protocolo IPC v1 app -> daemon. Contrato: docs/ipc_spec.md (espelha IpcProtocol.java).
#pragma once
#include <cstddef>
#include <cstdint>

namespace ipc {

constexpr size_t kMessageSize = 12;
constexpr uint8_t kMagic0 = 0x53, kMagic1 = 0x44, kVersion = 0x01;
constexpr const char* kDefaultSocketName = "snesbt_input";  // abstract namespace

enum Type : uint8_t {
    HELLO = 0x01,
    STATE = 0x02,
    NEUTRAL = 0x03,
    HELLO_ACK = 0x81,
};

enum Status : uint8_t { STATUS_OK = 0x00 };

constexpr uint16_t kAllReleased = 0xFFFF;
constexpr uint16_t kUnusedBits = 0xF000;
constexpr uint8_t kGripMax = 2;

struct Message {
    uint8_t type = 0;
    uint16_t seq = 0;
    uint16_t buttons = kAllReleased;  // registrador SNES, ativo em LOW
    uint8_t grip = 0;                 // 0 neutro, 1 abrir, 2 fechar
    uint8_t status = 0;
};

enum class DecodeError {
    NONE,
    INVALID_MAGIC,
    UNSUPPORTED_VERSION,
    UNKNOWN_TYPE,
    CRC_MISMATCH,
    INVALID_FIELD,
};

const char* to_string(DecodeError e);

uint8_t crc8(const uint8_t* data, size_t len);

// Decodifica exatamente kMessageSize bytes. Só preenche `out` se retornar NONE.
DecodeError decode(const uint8_t* data, Message& out);

void encode(const Message& m, uint8_t out[kMessageSize]);

}  // namespace ipc
