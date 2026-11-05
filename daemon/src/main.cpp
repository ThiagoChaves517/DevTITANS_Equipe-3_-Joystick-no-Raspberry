// snes_input_daemon: recebe estados do app Android via socket local (abstract namespace) e os
// publica num gamepad virtual uinput. Contrato IPC: docs/ipc_spec.md.
//
// Uso: snes_input_daemon [--socket NOME] [--allow-uid UID]... [--client-timeout-ms N]
//                        [--uinput /dev/uinput] [--name "SNES-BT-Controller"]
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "client_session.h"
#include "log.h"
#include "uinput_device.h"

namespace {

volatile sig_atomic_t g_running = 1;
void on_signal(int) { g_running = 0; }

long long now_ms() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

struct Options {
    std::string socket = ipc::kDefaultSocketName;
    std::vector<uid_t> allowed;
    long timeout_ms = 500;
    std::string uinput = "/dev/uinput";
    std::string name = "SNES-BT-Controller";
};

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) { LOGE("%s requer um valor", flag); return nullptr; }
            return argv[++i];
        };
        const char* v = nullptr;
        if (a == "--socket" && (v = value("--socket"))) o.socket = v;
        else if (a == "--allow-uid" && (v = value("--allow-uid"))) o.allowed.push_back(uid_t(std::strtoul(v, nullptr, 10)));
        else if (a == "--client-timeout-ms" && (v = value("--client-timeout-ms"))) o.timeout_ms = std::strtol(v, nullptr, 10);
        else if (a == "--uinput" && (v = value("--uinput"))) o.uinput = v;
        else if (a == "--name" && (v = value("--name"))) o.name = v;
        else { LOGE("argumento inválido: %s", a.c_str()); return false; }
    }
    if (o.timeout_ms <= 0) { LOGE("--client-timeout-ms deve ser > 0"); return false; }
    if (o.socket.empty() || o.socket.size() >= sizeof(sockaddr_un::sun_path) - 1) { LOGE("nome de socket inválido"); return false; }
    return true;
}

int listen_abstract(const std::string& name) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) { LOGE("socket: %s", strerror(errno)); return -1; }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path + 1, name.data(), name.size());  // sun_path[0] = 0: abstract namespace
    socklen_t len = socklen_t(offsetof(sockaddr_un, sun_path) + 1 + name.size());
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), len) < 0 || listen(fd, 2) < 0) {
        LOGE("bind/listen @%s: %s", name.c_str(), strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

bool write_all(int fd, const std::vector<uint8_t>& data) {
    size_t off = 0;
    while (off < data.size()) {
        ssize_t w = send(fd, data.data() + off, data.size() - off, MSG_NOSIGNAL);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return false;
        off += size_t(w);
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse(argc, argv, opt)) return 64;

    struct sigaction sa{};
    sa.sa_handler = on_signal;  // sem SA_RESTART: poll() retorna EINTR
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);

    snes::UinputDevice uinput;
    if (!uinput.create(opt.uinput.c_str(), opt.name.c_str())) {
        LOGE("FATAL: não foi possível criar o dispositivo de entrada; encerrando");
        return 2;
    }
    snes::ControllerDevice device(uinput);

    // O socket só é aberto depois do uinput: um cliente conectado implica dispositivo criado.
    int server = listen_abstract(opt.socket);
    if (server < 0) return 3;
    LOGI("escutando em @%s, timeout do cliente %ld ms, uids permitidos: %s", opt.socket.c_str(), opt.timeout_ms,
         opt.allowed.empty() ? "qualquer (use --allow-uid)" : std::to_string(opt.allowed.size()).c_str());

    int client = -1;
    snes::ClientSession* session = nullptr;
    long long last_msg_ms = 0;
    unsigned long timeouts = 0;

    auto drop_client = [&](const char* why) {
        if (client < 0) return;
        LOGW("cliente desconectado (%s); %lu mensagens, %zu bytes parciais descartados", why, session->messages(),
             session->partial_bytes());
        close(client);
        client = -1;
        delete session;
        session = nullptr;
        if (device.neutralize()) LOGI("botões liberados (desconexão)");
    };

    while (g_running) {
        int wait = -1;
        if (client >= 0 && !device.is_neutral()) {
            long long left = last_msg_ms + opt.timeout_ms - now_ms();
            wait = left > 0 ? int(left) : 0;
        }
        pollfd p[2] = {{server, POLLIN, 0}, {client, POLLIN, 0}};
        int n = poll(p, client >= 0 ? 2 : 1, wait);
        if (n < 0) {
            if (errno == EINTR) continue;
            LOGE("poll: %s", strerror(errno));
            break;
        }

        if (p[0].revents & POLLIN) {
            int c = accept4(server, nullptr, nullptr, SOCK_CLOEXEC);
            if (c >= 0) {
                ucred cred{};
                socklen_t cl = sizeof(cred);
                bool ok = getsockopt(c, SOL_SOCKET, SO_PEERCRED, &cred, &cl) == 0;
                bool allowed = opt.allowed.empty();
                for (uid_t u : opt.allowed) allowed = allowed || (ok && cred.uid == u);
                if (!allowed) {
                    LOGW("conexão recusada: uid=%d pid=%d não está em --allow-uid", int(cred.uid), int(cred.pid));
                    close(c);
                } else {
                    // Novo cliente substitui o anterior (ex.: app reiniciado antes do EOF chegar).
                    drop_client("substituído por nova conexão");
                    client = c;
                    session = new snes::ClientSession(device);
                    last_msg_ms = now_ms();
                    LOGI("cliente conectado uid=%d pid=%d", int(cred.uid), int(cred.pid));
                }
            }
        }

        if (client >= 0 && (p[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            uint8_t buf[256];
            ssize_t r = read(client, buf, sizeof(buf));
            if (r > 0) {
                std::vector<uint8_t> reply;
                std::string reason;
                bool keep = session->consume(buf, size_t(r), reply, reason);
                last_msg_ms = now_ms();
                if (!reply.empty() && !write_all(client, reply)) { drop_client("falha ao enviar HELLO_ACK"); continue; }
                if (!keep) { drop_client(reason.c_str()); continue; }
            } else if (r == 0) {
                drop_client("EOF");
                continue;
            } else if (errno != EINTR && errno != EAGAIN) {
                drop_client(strerror(errno));
                continue;
            }
        }

        if (client >= 0 && !device.is_neutral() && now_ms() - last_msg_ms >= opt.timeout_ms) {
            ++timeouts;
            device.neutralize();
            LOGW("watchdog: %ld ms sem mensagens do cliente; botões liberados (total %lu)", opt.timeout_ms, timeouts);
        }
    }

    LOGI("encerrando");
    drop_client("encerramento do daemon");
    device.neutralize();
    close(server);
    uinput.destroy();
    return 0;
}
