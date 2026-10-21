#pragma once
#include <Arduino.h>
namespace Protocol {
constexpr size_t FRAME_SIZE = 8;
constexpr uint8_t SYNC1 = 0x53, SYNC2 = 0x4E, VERSION = 0x01;
enum Type : uint8_t { CONTROLLER_STATE=0x01, GRIP_STATE=0x06, ACCEL_STATE=0x07, TRANSPORT_SWITCH=0x08, TRANSPORT_ACK=0x09 };
enum Transport : uint8_t { BLUETOOTH=0x00, WIRED=0x01 };
enum AckStatus : uint8_t { REJECTED=0x00, ACCEPTED=0x01, READY=0x02 };
uint8_t crc8(const uint8_t* data, size_t length);
void encode(uint8_t type, uint8_t sequence, uint8_t payload0, uint8_t payload1, uint8_t out[FRAME_SIZE]);
}
