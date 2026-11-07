// frame_sniffer: "ESP32 falso" para testar o snes_arm_daemon sem o braço.
//
// Cria uma pseudo-serial (pty), imprime o caminho e decodifica tudo o que chegar nela com o
// MESMO FrameDecoder e o MESMO mapeamento (ArmMapping.h) do firmware do braço.
//
// Uso:  frame_sniffer [--duration-ms N] [--all] [--william]
//       snes_arm_daemon --device <caminho impresso> --boot-wait-ms 0 [--protocolo frame|william]
//   --all      imprime também os quadros repetidos (keepalive)
//   --william  lê o formato do firmware do William (texto decimal, bit 1 = pressionado) e imprime
//              cada "rajada" (mesmo número seguido, sem pausa > 100 ms): quantas linhas, por quanto
//              tempo e o que o firmware dele faria com elas. Mede o intervalo entre linhas: abaixo de
//              25 ms o firmware dele acumularia linhas (ele lê no máximo 1 a cada 25 ms).
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
#include <string>

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

// O que o firmwareBraco.ino (William) faz com uma linha: pressButton() para cada bit em 1, 1° cada.
ArmMapping::Directions william_directions(unsigned pressed) {
    auto on = [&](int bit) { return (pressed & (1u << bit)) != 0 ? 1 : 0; };
    ArmMapping::Directions d{0, 0, 0, 0};
    d.cotovelo = int8_t(on(ArmMapping::B) - on(ArmMapping::Y));
    d.ombro = int8_t(on(ArmMapping::UP) - on(ArmMapping::DOWN));
    d.base = int8_t(on(ArmMapping::LEFT) - on(ArmMapping::RIGHT));
    d.garra = int8_t(on(ArmMapping::A) - on(ArmMapping::X));
    return d;  // START, SELECT, L, R: "default" no firmware dele (não mexem nada)
}

// Rajada = linhas seguidas com o mesmo número, sem pausa > 100 ms.
struct Burst {
    bool open = false;
    unsigned value = 0;
    long long first_ms = 0;
    long long last_ms = 0;
    unsigned long lines = 0;
};

void print_burst(const Burst& b) {
    if (!b.open) return;
    char pressed[96];
    describe(uint16_t(~b.value | 0xF000), pressed, sizeof(pressed));
    const ArmMapping::Directions d = william_directions(b.value);
    std::printf("[%6lld ms] linha=%-4u %s-> base=%+d ombro=%+d cotovelo=%+d garra=%+d  x%lu linhas em %lld ms\n",
                b.first_ms, b.value, pressed, d.base, d.ombro, d.cotovelo, d.garra, b.lines, b.last_ms - b.first_ms);
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    long duration_ms = 0;
    bool all = false;
    bool william = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--duration-ms") && i + 1 < argc) duration_ms = std::strtol(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "--all")) all = true;
        else if (!std::strcmp(argv[i], "--william")) william = true;
        else { std::fprintf(stderr, "uso: frame_sniffer [--duration-ms N] [--all] [--william]\n"); return 64; }
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
    // --william
    Burst burst;
    std::string line;
    unsigned long lines = 0, bad_lines = 0, fast = 0;
    long long last_line_ms = -1, min_gap = -1;

    while (g_running && (duration_ms <= 0 || now_ms() - start < duration_ms)) {
        pollfd p{master, POLLIN, 0};
        if (poll(&p, 1, 100) <= 0) continue;
        uint8_t buf[256];
        const ssize_t n = read(master, buf, sizeof(buf));
        if (n <= 0) continue;

        if (william) {
            for (ssize_t i = 0; i < n; ++i) {
                const char c = char(buf[i]);
                if (c != '\n') {
                    if (line.size() < 32) line += c;
                    continue;
                }
                const long long t = now_ms() - start;
                char* end = nullptr;
                const unsigned long v = std::strtoul(line.c_str(), &end, 10);
                const bool ok = !line.empty() && end != nullptr && *end == '\0' && v <= 0x0FFF;
                line.clear();
                if (!ok) {
                    ++bad_lines;
                    continue;
                }
                ++lines;
                if (last_line_ms >= 0) {
                    const long long gap = t - last_line_ms;
                    if (min_gap < 0 || gap < min_gap) min_gap = gap;
                    if (gap < 25) ++fast;
                }
                last_line_ms = t;
                if (!burst.open || burst.value != v || t - burst.last_ms > 100) {
                    print_burst(burst);
                    burst = Burst{true, unsigned(v), t, t, 0};
                }
                burst.last_ms = t;
                ++burst.lines;
            }
            continue;
        }

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

    if (william) {
        print_burst(burst);
        std::printf("FIM linhas=%lu invalidas=%lu menor_intervalo_ms=%lld abaixo_de_25ms=%lu\n", lines, bad_lines,
                    min_gap, fast);
    } else {
        std::printf("FIM quadros=%lu repetidos(keepalive)=%lu invalidos=%lu descartados=%lu\n", decoder.frames(),
                    repeats, decoder.errors(), decoder.discarded());
    }
    close(slave);
    close(master);
    return 0;
}
