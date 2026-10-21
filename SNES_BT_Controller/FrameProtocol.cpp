#include "FrameProtocol.h"
namespace Protocol {
uint8_t crc8(const uint8_t* data, size_t length) {
    uint8_t crc=0;
    for(size_t i=0;i<length;++i){ crc^=data[i]; for(uint8_t b=0;b<8;++b) crc=(crc&0x80)?static_cast<uint8_t>((crc<<1)^0x07):static_cast<uint8_t>(crc<<1); }
    return crc;
}
void encode(uint8_t type,uint8_t sequence,uint8_t p0,uint8_t p1,uint8_t out[FRAME_SIZE]) {
    out[0]=SYNC1; out[1]=SYNC2; out[2]=VERSION; out[3]=type; out[4]=sequence; out[5]=p0; out[6]=p1; out[7]=crc8(out,7);
}
}
