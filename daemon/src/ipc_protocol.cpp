#include "ipc_protocol.h"

namespace ipc {

const char* to_string(DecodeError e) {
    switch (e) {
        case DecodeError::NONE: return "NONE";
        case DecodeError::INVALID_MAGIC: return "INVALID_MAGIC";
        case DecodeError::UNSUPPORTED_VERSION: return "UNSUPPORTED_VERSION";
        case DecodeError::UNKNOWN_TYPE: return "UNKNOWN_TYPE";
        case DecodeError::CRC_MISMATCH: return "CRC_MISMATCH";
        case DecodeError::INVALID_FIELD: return "INVALID_FIELD";
    }
    return "?";
}

uint8_t crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) crc = (crc & 0x80) ? uint8_t((crc << 1) ^ 0x07) : uint8_t(crc << 1);
    }
    return crc;
}

DecodeError decode(const uint8_t* d, Message& out) {
    if (d[0] != kMagic0 || d[1] != kMagic1) return DecodeError::INVALID_MAGIC;
    if (d[2] != kVersion) return DecodeError::UNSUPPORTED_VERSION;
    if (d[3] != HELLO && d[3] != STATE && d[3] != NEUTRAL) return DecodeError::UNKNOWN_TYPE;
    if (crc8(d, kMessageSize - 1) != d[11]) return DecodeError::CRC_MISMATCH;
    Message m;
    m.type = d[3];
    m.seq = uint16_t(d[4] | (d[5] << 8));
    m.buttons = uint16_t(d[6] | (d[7] << 8));
    m.grip = d[8];
    m.status = d[9];
    if (m.grip > kGripMax || d[10] != 0) return DecodeError::INVALID_FIELD;
    if (m.type == NEUTRAL && ((m.buttons | kUnusedBits) != kAllReleased || m.grip != 0)) return DecodeError::INVALID_FIELD;
    out = m;
    return DecodeError::NONE;
}

void encode(const Message& m, uint8_t out[kMessageSize]) {
    out[0] = kMagic0;
    out[1] = kMagic1;
    out[2] = kVersion;
    out[3] = m.type;
    out[4] = uint8_t(m.seq);
    out[5] = uint8_t(m.seq >> 8);
    out[6] = uint8_t(m.buttons);
    out[7] = uint8_t(m.buttons >> 8);
    out[8] = m.grip;
    out[9] = m.status;
    out[10] = 0;
    out[11] = crc8(out, kMessageSize - 1);
}

}  // namespace ipc
