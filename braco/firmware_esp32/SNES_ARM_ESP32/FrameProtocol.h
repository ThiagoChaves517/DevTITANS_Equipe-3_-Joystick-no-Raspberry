// Quadro v1 de 8 bytes: o MESMO que o joystick manda para o Android
// (SNES_BT_Controller/FrameProtocol.h e driver/docs/protocol_spec.md).
// A Raspberry repassa ao braço só o CONTROLLER_STATE, com os 16 bits do controle.
//
//   53 4E | 01 | tipo | seq | p0 | p1 | CRC-8/0x07 (bytes 0-6)
//
// C++ puro: usado pelo firmware, pelo snes_arm_daemon e pelos testes.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace Protocol {

constexpr size_t FRAME_SIZE = 8;
constexpr uint8_t SYNC1 = 0x53, SYNC2 = 0x4E, VERSION = 0x01;

enum Type : uint8_t {
    CONTROLLER_STATE = 0x01,
    HELLO = 0x02,
    PING = 0x03,
    CONFIG = 0x04,
    ERROR = 0x05,
    GRIP_STATE = 0x06,
    ACCEL_STATE = 0x07,
    TRANSPORT_SWITCH = 0x08,
    TRANSPORT_ACK = 0x09,
    ACCEL_CALIBRATED = 0x0A,
};

uint8_t crc8(const uint8_t* data, size_t length);

void encode(uint8_t type, uint8_t sequence, uint8_t payload0, uint8_t payload1, uint8_t out[FRAME_SIZE]);

struct Frame {
    uint8_t type;
    uint8_t sequence;
    uint8_t payload0;
    uint8_t payload1;

    uint16_t state() const { return uint16_t(payload0 | (payload1 << 8)); }  // little-endian
};

// Decodificador de fluxo para a serial: acha o SYNC, confere versão, tipo e CRC.
// Se um byte se perder, descarta até o próximo "SN" válido (ressincroniza sozinho).
class FrameDecoder {
public:
    // Devolve true quando `out` recebeu um quadro válido.
    bool push(uint8_t byte, Frame& out);

    unsigned long frames() const { return frames_; }
    unsigned long errors() const { return errors_; }      // CRC, versão ou tipo inválidos
    unsigned long discarded() const { return discarded_; } // bytes pulados procurando o SYNC

private:
    uint8_t buf_[FRAME_SIZE] = {};
    size_t fill_ = 0;
    unsigned long frames_ = 0, errors_ = 0, discarded_ = 0;
};

bool isKnownType(uint8_t type);

}  // namespace Protocol
