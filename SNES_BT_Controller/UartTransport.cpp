#include "UartTransport.h"
#include "BoardConfig.h"

namespace {
// GPIO16/17 (pinos padrao da UART2) agora sao os botoes A e X.
constexpr int UART_RX_PIN = 22; // ESP32 RX2 <- Raspberry Pi TX
constexpr int UART_TX_PIN = 23; // ESP32 TX2 -> Raspberry Pi RX
}

void UartTransport::begin(uint32_t baud) {
    port = &Serial2;
    port->begin(baud, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
    ready = true;
    Serial.printf("[UART] UART2 ativa: TX=GPIO%d RX=GPIO%d baud=%lu\n",
                  UART_TX_PIN, UART_RX_PIN, static_cast<unsigned long>(baud));
}

void UartTransport::sendFrame(const uint8_t* frame, size_t length) {
    if (!ready || port == nullptr || frame == nullptr || length == 0) return;
    port->write(frame, length);
}
