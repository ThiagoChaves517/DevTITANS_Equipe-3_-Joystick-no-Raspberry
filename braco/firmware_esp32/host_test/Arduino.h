#pragma once

/*
 * Arduino.h de mentira para compilar e rodar o SNES_ARM_ESP32 no computador
 * (sem ESP32 e sem arduino-cli). Serial = stdin/stdout, millis() = relógio
 * real, ledcWrite() guarda o duty de cada pino para os testes conferirem.
 *
 * Imita o core ESP32 3.x (ledcAttach/ledcWrite por pino).
 */

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define ESP_ARDUINO_VERSION_MAJOR 3

uint32_t millis();
void delay(uint32_t ms);

bool ledcAttach(uint8_t pin, uint32_t freq, uint8_t resolution);
bool ledcWrite(uint8_t pin, uint32_t duty);
uint32_t hostLedcDuty(uint8_t pin);

class HostSerial
{
public:
    void begin(unsigned long) {}
    int available();
    int read();
    size_t print(const char *text) { return (size_t)std::printf("%s", text); }
    size_t println(const char *text = "") { return (size_t)std::printf("%s\n", text); }

    size_t printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)))
    {
        va_list args;
        va_start(args, fmt);
        const int n = std::vprintf(fmt, args);
        va_end(args);
        return n < 0 ? 0 : (size_t)n;
    }
};

extern HostSerial Serial;
