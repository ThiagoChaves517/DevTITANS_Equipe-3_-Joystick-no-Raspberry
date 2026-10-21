/*
 * Roda setup()/loop() do firmware no computador.
 *
 *   ./run.sh                         interativo: digite J/H/S/P/I
 *   ./run.sh < comandos.txt          linhas "@<ms>" esperam antes do próximo comando
 *
 * Ao final imprime "# DUTY pino=duty(µs)" de cada servo, para conferir o PWM.
 */

#include "Arduino.h"

#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <map>
#include <string>
#include <thread>
#include <unistd.h>

void setup();
void loop();

HostSerial Serial;

static const auto start = std::chrono::steady_clock::now();
static std::map<uint8_t, uint32_t> duties;
static std::string pending;      /* bytes ainda não lidos pelo firmware */
static uint32_t release_at = 0;  /* "@ms": segura o próximo comando até este instante */
static bool input_eof = false;
static std::string stdin_buf;

uint32_t millis()
{
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
}

void delay(uint32_t ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

bool ledcAttach(uint8_t pin, uint32_t, uint8_t)
{
    duties[pin] = 0;
    return true;
}

bool ledcWrite(uint8_t pin, uint32_t duty)
{
    duties[pin] = duty;
    return true;
}

uint32_t hostLedcDuty(uint8_t pin)
{
    return duties[pin];
}

/* Lê o stdin sem bloquear e libera uma linha por vez, respeitando "@ms". */
static void pump_stdin()
{
    char buf[256];
    ssize_t n;

    while ((n = ::read(STDIN_FILENO, buf, sizeof(buf))) > 0)
        stdin_buf.append(buf, (size_t)n);

    if (n == 0)
        input_eof = true;

    while (pending.empty() && millis() >= release_at)
    {
        const size_t nl = stdin_buf.find('\n');

        if (nl == std::string::npos)
            break;

        std::string line = stdin_buf.substr(0, nl + 1);
        stdin_buf.erase(0, nl + 1);

        if (line[0] == '@')
            release_at = millis() + (uint32_t)std::atoi(line.c_str() + 1);
        else
            pending = line;
    }
}

int HostSerial::available()
{
    pump_stdin();
    return (int)pending.size();
}

int HostSerial::read()
{
    if (pending.empty())
        return -1;

    const int c = (unsigned char)pending[0];
    pending.erase(0, 1);
    return c;
}

int main()
{
    setvbuf(stdout, nullptr, _IOLBF, 0);
    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);

    setup();

    /* Depois do fim da entrada ainda roda 600 ms (rampa/watchdog terminarem). */
    uint32_t stop_at = 0;

    for (;;)
    {
        loop();
        std::this_thread::sleep_for(std::chrono::microseconds(500));

        if (input_eof && pending.empty() && stdin_buf.empty() && millis() >= release_at)
        {
            if (stop_at == 0)
                stop_at = millis() + 600;
            else if (millis() >= stop_at)
                break;
        }
    }

    for (const auto &[pin, duty] : duties)
        std::printf("# DUTY GPIO%u=%u (%.0f us)\n", pin, duty, duty * 20000.0 / 65535.0);

    return 0;
}
