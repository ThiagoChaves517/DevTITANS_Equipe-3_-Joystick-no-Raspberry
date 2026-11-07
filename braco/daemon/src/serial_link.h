// Link com o ESP32 do braço: serial USB (produção) ou TCP (simulador).
//
// Destinos (--device):
//   auto            a única serial USB presente (ttyUSB*/ttyACM*). Com mais de uma (ex.: o
//                   ESP32 do joystick também na USB) recusa e pede --device: abrir a porta
//                   errada reiniciaria o outro ESP32, e o braço não responde para se identificar.
//   /dev/ttyUSB1    caminho fixo
//   usb:1-1.3       porta USB física (não muda de nome quando a ordem de enumeração muda)
//   tcp:HOST:PORTA  simulador (braco/simulador/arm_sim.py)
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "forwarder.h"

namespace snes_arm {

struct SerialCandidate {
    std::string tty;       // /dev/ttyUSB0
    std::string usb_port;  // 1-1.3
    std::string vid_pid;   // 10c4:ea60
    std::string product;   // CP2102 USB to UART Bridge Controller
};

class SerialLink : public FrameSink {
public:
    SerialLink(std::string device, long boot_wait_ms, bool echo)
        : device_(std::move(device)), boot_wait_ms_(boot_wait_ms), echo_(echo) {}
    ~SerialLink() override { close_link(nullptr); }

    // Abre/reabre o link quando fechado (no máximo 1 tentativa por segundo) e controla a
    // espera do boot do ESP32. Retorna true no instante em que o link fica pronto.
    bool maintain(long long now_ms);

    bool is_ready() const override { return fd_ >= 0 && ready_; }
    bool send(const uint8_t* data, size_t len) override;

    // Esvazia o que o ESP32 escreveu (logs "[ARM]"); com --echo eles vão para o log.
    // hangup = poll() marcou POLLHUP/POLLERR (cabo arrancado, ESP32 desligado, pty fechada):
    // aí read() == 0 é fim do link, e não "sem dados". Sem fechar, o poll() acorda sem parar (100% de CPU).
    void on_readable(bool hangup);
    void close_link(const char* why);

    int fd() const { return fd_; }
    const std::string& path() const { return path_; }
    long wait_hint_ms(long long now_ms) const;  // quanto o laço pode dormir por causa do link

    // Seriais USB vistas pelo sysfs (sem abrir nenhuma).
    static std::vector<SerialCandidate> list_candidates();

private:
    bool resolve(std::string& path, bool& is_tcp);
    bool open_tty(const std::string& path);
    bool open_tcp(const std::string& target);

    std::string device_;
    long boot_wait_ms_;
    bool echo_;
    int fd_ = -1;
    bool is_tcp_ = false;
    bool ready_ = false;
    long long ready_at_ms_ = 0;
    long long last_try_ms_ = -1000000;
    std::string path_;
    std::string warned_;  // última mensagem de "não achei", para não repetir a cada segundo
    std::string line_;
};

}  // namespace snes_arm
