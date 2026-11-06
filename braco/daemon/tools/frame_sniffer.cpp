// frame_sniffer: "ESP32 falso" para testar o snes_arm_daemon sem o braço.
//
// Cria uma pseudo-serial (pty), imprime o caminho e decodifica tudo o que chegar nela com o
// MESMO FrameDecoder e o MESMO mapeamento (ArmMapping.h) do firmware do braço.
//
// Uso:  frame_sniffer [--duration-ms N] [--all]
//       snes_arm_daemon --device <caminho impresso> --boot-wait-ms 0
//   --all  imprime também os quadros repetidos (keepalive)
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>  // posix_openpt, grantpt, unlockpt, ptsname
#include <termios.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "ArmMapping.h"
#include "FrameProtocol.h"

namespace {

volatile sig_atomic_t g_running = 1;
void on_signal(int) { g_running = 0; }

long long now_ms() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

void describe(uint16_t state, char* out, size_t len) {
    static const char* names[] = {"B", "Y", "SELECT", "START", "UP", "DOWN", "LEFT", "RIGHT", "A", "X", "L", "R"};
    out[0] = '\0';
    for (int bit = 0; bit < 12; ++bit) {
        if (state & (1u << bit)) continue;
        std::strncat(out, names[bit], len - std::strlen(out) - 1);
        std::strncat(out, " ", len - std::strlen(out) - 1);
    }
    if (out[0] == '\0') std::snprintf(out, len, "(nada) ");
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    long duration_ms = 0;
    bool all = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--duration-ms") && i + 1 < argc) duration_ms = std::strtol(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "--all")) all = true;
        else { std::fprintf(stderr, "uso: frame_sniffer [--duration-ms N] [--all]\n"); return 64; }
    }

    struct sigaction sa{};
    sa.sa_handler = on_signal;  // Ctrl+C / pkill: ainda imprime o resumo "FIM"
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    const int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || grantpt(master) < 0 || unlockpt(master) < 0) {
        std::perror("posix_openpt");
        return 1;
    }
    const char* slave_name = ptsname(master);
    // Mantém o lado escravo aberto: assim o daemon pode fechar/reabrir sem derrubar a pty.
    const int slave = open(slave_name, O_RDWR | O_NOCTTY);
    termios tio{};
    tcgetattr(slave, &tio);
    cfmakeraw(&tio);
    tcsetattr(slave, TCSANOW, &tio);

    std::printf("PTY %s\n", slave_name);

    Protocol::FrameDecoder decoder;
    uint16_t last = 0;
    bool first = true;
    unsigned long repeats = 0;
    const long long start = now_ms();

    while (g_running && (duration_ms <= 0 || now_ms() - start < duration_ms)) {
        pollfd p{master, POLLIN, 0};
        if (poll(&p, 1, 100) <= 0) continue;
        uint8_t buf[256];
        const ssize_t n = read(master, buf, sizeof(buf));
        if (n <= 0) continue;

        for (ssize_t i = 0; i < n; ++i) {
            Protocol::Frame f;
            if (!decoder.push(buf[i], f)) continue;
            const uint16_t st = uint16_t(f.state() | 0xF000);
            if (!first && st == last && !all) {
                ++repeats;
                continue;
            }
            char pressed[96];
            describe(st, pressed, sizeof(pressed));
            const ArmMapping::Directions d = ArmMapping::fromState(st);
            std::printf("[%6lld ms] seq=%3u estado=0x%04X  %s-> base=%+d ombro=%+d cotovelo=%+d garra=%+d\n",
                        now_ms() - start, f.sequence, st, pressed, d.base, d.ombro, d.cotovelo, d.garra);
            last = st;
            first = false;
        }
    }

    std::printf("FIM quadros=%lu repetidos(keepalive)=%lu invalidos=%lu descartados=%lu\n", decoder.frames(), repeats,
                decoder.errors(), decoder.discarded());
    close(slave);
    close(master);
    return 0;
}
