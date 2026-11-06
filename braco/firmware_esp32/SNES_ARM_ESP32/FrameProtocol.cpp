#include "FrameProtocol.h"

#include <string.h>

namespace Protocol {

uint8_t crc8(const uint8_t* data, size_t length) {
    uint8_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; ++b) crc = (crc & 0x80) ? uint8_t((crc << 1) ^ 0x07) : uint8_t(crc << 1);
    }
    return crc;
}

void encode(uint8_t type, uint8_t sequence, uint8_t p0, uint8_t p1, uint8_t out[FRAME_SIZE]) {
    out[0] = SYNC1;
    out[1] = SYNC2;
    out[2] = VERSION;
    out[3] = type;
    out[4] = sequence;
    out[5] = p0;
    out[6] = p1;
    out[7] = crc8(out, 7);
}

bool isKnownType(uint8_t type) { return type >= CONTROLLER_STATE && type <= ACCEL_CALIBRATED; }

bool FrameDecoder::push(uint8_t byte, Frame& out) {
    buf_[fill_++] = byte;

    for (;;) {
        // O buffer sempre começa em "SN": o que não pode ser início de quadro é descartado.
        if (fill_ >= 1 && buf_[0] != SYNC1) {
            memmove(buf_, buf_ + 1, --fill_);
            ++discarded_;
            continue;
        }
        if (fill_ >= 2 && buf_[1] != SYNC2) {
            memmove(buf_, buf_ + 1, --fill_);
            ++discarded_;
            continue;
        }
        if (fill_ < FRAME_SIZE) return false;

        if (buf_[2] != VERSION || !isKnownType(buf_[3]) || crc8(buf_, 7) != buf_[7]) {
            // Era um "SN" por acaso no meio dos dados: recomeça a busca no byte seguinte.
            ++errors_;
            memmove(buf_, buf_ + 1, --fill_);
            continue;
        }

        out.type = buf_[3];
        out.sequence = buf_[4];
        out.payload0 = buf_[5];
        out.payload1 = buf_[6];
        fill_ = 0;
        ++frames_;
        return true;
    }
}

}  // namespace Protocol
