// Leitura do gamepad virtual criado pelo snes_input_daemon (somente Linux/Android).
//
// Não usa EVIOCGRAB: o Android continua recebendo o controle normalmente.
#pragma once
#include <string>
#include <utility>

#include "snes_register.h"

namespace snes_arm {

class EvdevReader {
public:
    // fixed_path vazio = procurar em /dev/input pelo nome do dispositivo.
    EvdevReader(std::string name, std::string fixed_path)
        : name_(std::move(name)), fixed_path_(std::move(fixed_path)) {}
    ~EvdevReader() { close_device(nullptr); }

    // Procura/reabre o dispositivo quando fechado (no máximo 1 vez por segundo).
    // Retorna true no instante em que abriu; `reg` recebe o estado atual lido do kernel.
    bool maintain(long long now_ms, SnesRegister& reg);

    // Lê os eventos disponíveis. Retorna 1 se um SYN_REPORT fechou um estado novo,
    // 0 se nada mudou e -1 se o dispositivo sumiu (daemon do joystick parou).
    int read_events(SnesRegister& reg);

    int fd() const { return fd_; }
    const std::string& path() const { return path_; }
    long wait_hint_ms(long long now_ms) const;

private:
    bool try_open(const std::string& path, bool check_name, SnesRegister& reg);
    void resync(SnesRegister& reg);
    void close_device(const char* why);

    std::string name_;
    std::string fixed_path_;
    std::string path_;
    int fd_ = -1;
    long long last_scan_ms_ = -1000000;
    bool warned_ = false;
};

}  // namespace snes_arm
