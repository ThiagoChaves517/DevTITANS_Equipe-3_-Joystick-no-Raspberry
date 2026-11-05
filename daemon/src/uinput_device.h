// Dispositivo virtual Linux uinput (somente Linux/Android).
#pragma once
#include "controller_device.h"

namespace snes {

class UinputDevice : public InputSink {
public:
    UinputDevice() = default;
    ~UinputDevice() override;
    UinputDevice(const UinputDevice&) = delete;
    UinputDevice& operator=(const UinputDevice&) = delete;

    // Cria o dispositivo. Em caso de falha, registra o motivo e retorna false.
    bool create(const char* path, const char* name);
    void destroy();

    bool emit(int type, int code, int value) override;
    bool sync() override;

private:
    int fd_ = -1;
};

}  // namespace snes
