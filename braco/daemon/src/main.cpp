// snes_arm_daemon: lê o gamepad virtual criado pelo snes_input_daemon (branch driver) e
// repassa os 16 bits do controle SNES ao ESP32 do braço pela serial USB. Sem resposta do braço.
// Contrato: braco/docs/protocolo_serial.md.
//
// Uso: snes_arm_daemon [--device auto|/dev/ttyUSB1|usb:1-1.3|tcp:HOST:PORTA] [--name "SNES-BT-Controller"]
//                      [--input /dev/input/eventN] [--keepalive-ms 100] [--boot-wait-ms 1500]
//                      [--echo] [--verbose] [--list]
#include <poll.h>
#include <signal.h>
#include <termios.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "evdev_reader.h"
#include "forwarder.h"
#include "log.h"
#include "serial_link.h"
#include "snes_register.h"

namespace {

volatile sig_atomic_t g_running = 1;
void on_signal(int) { g_running = 0; }

long long now_ms() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

struct Options {
    std::string device = "auto";
    std::string name = "SNES-BT-Controller";
    std::string input;
    long keepalive_ms = 100;
    long boot_wait_ms = 1500;
    bool echo = false;
    bool verbose = false;
    bool list = false;
};

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) { LOGE("%s requer um valor", flag); return nullptr; }
            return argv[++i];
        };
        const char* v = nullptr;
        if (a == "--device" && (v = value("--device"))) o.device = v;
        else if (a == "--name" && (v = value("--name"))) o.name = v;
        else if (a == "--input" && (v = value("--input"))) o.input = v;
        else if (a == "--keepalive-ms" && (v = value("--keepalive-ms"))) o.keepalive_ms = std::strtol(v, nullptr, 10);
        else if (a == "--boot-wait-ms" && (v = value("--boot-wait-ms"))) o.boot_wait_ms = std::strtol(v, nullptr, 10);
        else if (a == "--echo") o.echo = true;
        else if (a == "--verbose") o.verbose = true;
        else if (a == "--list") o.list = true;
        else { LOGE("argumento inválido: %s", a.c_str()); return false; }
    }
    // O ESP32 zera as direções após 250 ms sem quadro: o keepalive precisa ficar bem abaixo disso.
    if (o.keepalive_ms <= 0 || o.keepalive_ms > 200) { LOGE("--keepalive-ms deve ficar entre 1 e 200"); return false; }
    if (o.boot_wait_ms < 0) { LOGE("--boot-wait-ms deve ser >= 0"); return false; }
    return true;
}

int list_serials() {
    const auto all = snes_arm::SerialLink::list_candidates();
    if (all.empty()) std::printf("nenhuma serial USB (ttyUSB*/ttyACM*)\n");
    for (const auto& c : all)
        std::printf("%-14s usb:%-10s %s  %s\n", c.tty.c_str(), c.usb_port.c_str(), c.vid_pid.c_str(), c.product.c_str());
    return 0;
}

// Menor espera positiva entre as candidatas (-1 = sem prazo).
long min_wait(long a, long b) {
    if (a < 0) return b;
    if (b < 0) return a;
    return a < b ? a : b;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse(argc, argv, opt)) return 64;
    if (opt.list) return list_serials();

    struct sigaction sa{};
    sa.sa_handler = on_signal;  // sem SA_RESTART: poll() retorna EINTR
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);

    snes_arm::SerialLink link(opt.device, opt.boot_wait_ms, opt.echo);
    snes_arm::Forwarder forwarder(link, opt.keepalive_ms);
    snes_arm::EvdevReader reader(opt.name, opt.input);
    snes_arm::SnesRegister reg;

    LOGI("snes_arm_daemon: controle \"%s\" -> braço em %s, keepalive %ld ms", opt.name.c_str(), opt.device.c_str(),
         opt.keepalive_ms);

    uint16_t logged = reg.value();
    auto publish = [&](const char* why) {
        forwarder.publish(reg.value());
        if (opt.verbose && reg.value() != logged) LOGI("estado 0x%04X (%s)", reg.value(), why);
        logged = reg.value();
    };

    while (g_running) {
        long long now = now_ms();

        if (reader.maintain(now, reg)) publish("controle conectado");
        if (link.maintain(now)) {
            LOGI("braço pronto em %s: enviando o estado atual 0x%04X", link.path().c_str(), forwarder.state());
            forwarder.link_restarted();
        }
        forwarder.tick(now);

        const long wait = min_wait(min_wait(forwarder.next_wakeup_ms(now), link.wait_hint_ms(now)),
                                   reader.wait_hint_ms(now));
        pollfd p[2] = {{reader.fd(), POLLIN, 0}, {link.fd(), POLLIN, 0}};
        const int n = poll(p, 2, wait < 0 ? 1000 : int(wait));
        if (n < 0) {
            if (errno == EINTR) continue;
            LOGE("poll: %s", strerror(errno));
            break;
        }

        if (p[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            const int r = reader.read_events(reg);
            if (r > 0) {
                publish("evento");
            } else if (r < 0) {
                // O gamepad sumiu com algo pressionado: o braço precisa parar já, não em 250 ms.
                reg.reset();
                publish("controle perdido");
                LOGW("controle perdido: enviando estado neutro ao braço");
            }
        }
        if (p[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            link.on_readable();
            if (link.fd() < 0) LOGW("o braço vai parar sozinho em 250 ms (watchdog do ESP32)");
        }

        forwarder.tick(now_ms());  // mudança de estado sai na mesma volta do laço
    }

    LOGI("encerrando: soltando todos os botões no braço");
    reg.reset();
    forwarder.publish(reg.value());
    forwarder.tick(now_ms());
    if (link.fd() >= 0) tcdrain(link.fd());
    LOGI("%lu quadros enviados, %lu falhas de envio", forwarder.frames_sent(), forwarder.send_errors());
    return 0;
}
