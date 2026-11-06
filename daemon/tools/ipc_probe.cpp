// Cliente IPC de diagnóstico: valida o daemon sem o app Android.
// Uso: ipc_probe [--socket NOME] [--hold-ms N] estado_hex [estado_hex...]
//   ex.: ipc_probe FEFF FFFF   (pressiona A e solta)
// Faz HELLO, confere o HELLO_ACK e envia cada estado com intervalo de 200 ms.
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../src/ipc_protocol.h"

int main(int argc, char** argv) {
    std::string name = ipc::kDefaultSocketName;
    long hold_ms = 200;
    int first = 1;
    while (first < argc && argv[first][0] == '-') {
        if (!std::strcmp(argv[first], "--socket") && first + 1 < argc) name = argv[first + 1];
        else if (!std::strcmp(argv[first], "--hold-ms") && first + 1 < argc) hold_ms = std::strtol(argv[first + 1], nullptr, 10);
        else { std::fprintf(stderr, "argumento inválido: %s\n", argv[first]); return 64; }
        first += 2;
    }
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path + 1, name.data(), name.size());
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), socklen_t(offsetof(sockaddr_un, sun_path) + 1 + name.size())) < 0) {
        std::perror("connect");
        return 1;
    }
    uint8_t buf[ipc::kMessageSize];
    ipc::Message m;
    m.type = ipc::HELLO;
    ipc::encode(m, buf);
    if (write(fd, buf, sizeof(buf)) != ssize_t(sizeof(buf))) { std::perror("write HELLO"); return 1; }
    size_t got = 0;
    while (got < sizeof(buf)) {
        ssize_t r = read(fd, buf + got, sizeof(buf) - got);
        if (r <= 0) { std::fprintf(stderr, "sem HELLO_ACK\n"); return 1; }
        got += size_t(r);
    }
    if (buf[3] != ipc::HELLO_ACK || buf[11] != ipc::crc8(buf, 11) || buf[9] != ipc::STATUS_OK) {
        std::fprintf(stderr, "HELLO_ACK inválido\n");
        return 1;
    }
    std::printf("HELLO_ACK ok\n");
    for (int i = first; i < argc; ++i) {
        ipc::Message s;
        s.buttons = uint16_t(std::strtoul(argv[i], nullptr, 16));
        s.type = s.buttons == ipc::kAllReleased ? ipc::NEUTRAL : ipc::STATE;
        s.seq = uint16_t(i);
        ipc::encode(s, buf);
        if (write(fd, buf, sizeof(buf)) != ssize_t(sizeof(buf))) { std::perror("write"); return 1; }
        std::printf("enviado 0x%04X\n", s.buttons);
        usleep(useconds_t(hold_ms * 1000));
    }
    close(fd);
    return 0;
}
