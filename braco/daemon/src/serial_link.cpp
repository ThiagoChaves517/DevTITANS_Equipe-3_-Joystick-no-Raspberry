#include "serial_link.h"

#include <dirent.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "log.h"

namespace snes_arm {
namespace {

constexpr long kRetryMs = 1000;

std::string read_line(const std::string& file) {
    std::ifstream in(file);
    std::string s;
    std::getline(in, s);
    return s;
}

bool starts_with(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

}  // namespace

std::vector<SerialCandidate> SerialLink::list_candidates() {
    std::vector<SerialCandidate> out;
    DIR* dir = opendir("/sys/class/tty");
    if (dir == nullptr) return out;  // fora do Linux (testes no computador)

    while (dirent* e = readdir(dir)) {
        const std::string name = e->d_name;
        if (!starts_with(name, "ttyUSB") && !starts_with(name, "ttyACM")) continue;

        SerialCandidate c;
        c.tty = "/dev/" + name;

        // .../usb1/1-1/1-1.3/1-1.3:1.0[/ttyUSB0]: sobe até o diretório do dispositivo USB (tem idVendor).
        char real[PATH_MAX];
        if (realpath(("/sys/class/tty/" + name + "/device").c_str(), real) != nullptr) {
            std::string p = real;
            while (!p.empty() && p != "/") {
                std::ifstream probe(p + "/idVendor");
                if (probe.good()) {
                    c.usb_port = p.substr(p.rfind('/') + 1);
                    c.vid_pid = read_line(p + "/idVendor") + ":" + read_line(p + "/idProduct");
                    c.product = read_line(p + "/product");
                    break;
                }
                p = p.substr(0, p.rfind('/'));
            }
        }
        out.push_back(c);
    }
    closedir(dir);
    return out;
}

bool SerialLink::resolve(std::string& path, bool& is_tcp) {
    is_tcp = starts_with(device_, "tcp:");
    if (is_tcp) {
        path = device_.substr(4);
        return true;
    }
    if (device_ != "auto" && !starts_with(device_, "usb:")) {
        path = device_;
        return true;
    }

    const std::vector<SerialCandidate> all = list_candidates();
    std::string problem;

    if (starts_with(device_, "usb:")) {
        const std::string port = device_.substr(4);
        for (const SerialCandidate& c : all) {
            if (c.usb_port == port) {
                path = c.tty;
                return true;
            }
        }
        problem = "nenhuma serial na porta USB " + port;
    } else if (all.size() == 1) {
        path = all[0].tty;
        // O braço não responde, então "auto" não tem como saber se esta é mesmo a porta dele.
        LOGW("--device auto: usando a única serial USB, %s (usb:%s %s). Se o ESP32 do braço estiver "
             "desligado e esta for a do joystick, ela será reiniciada: com os dois ESP32 na USB, fixe --device usb:<porta>",
             all[0].tty.c_str(), all[0].usb_port.c_str(), all[0].vid_pid.c_str());
        return true;
    } else if (all.empty()) {
        problem = "nenhuma serial USB (ttyUSB*/ttyACM*) conectada";
    } else {
        problem = "mais de uma serial USB; escolha a do braço com --device (veja --list):";
        for (const SerialCandidate& c : all)
            problem += " " + c.tty + "(usb:" + c.usb_port + " " + c.vid_pid + ")";
    }

    if (problem != warned_) {
        LOGW("%s", problem.c_str());
        warned_ = problem;
    }
    return false;
}

bool SerialLink::open_tty(const std::string& path) {
    const int fd = open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        const std::string msg = "abrir " + path + ": " + strerror(errno);
        if (msg != warned_) {
            // /dev/ttyUSB* é root:root 0600: sem root dá EACCES. "adb root" não: derruba o driver do Raphael.
            LOGW("%s%s", msg.c_str(),
                 errno == EACCES ? " (o driver precisa de root: suba com adb shell sh /data/local/tmp/rasp_start.sh)" : "");
            warned_ = msg;
        }
        return false;
    }

    termios tio{};
    if (tcgetattr(fd, &tio) < 0) {
        LOGE("tcgetattr %s: %s", path.c_str(), strerror(errno));
        close(fd);
        return false;
    }
    cfmakeraw(&tio);
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~(CSTOPB | PARENB | CRTSCTS);
    tio.c_cflag = (tio.c_cflag & ~CSIZE) | CS8;
    if (tcsetattr(fd, TCSANOW, &tio) < 0) {
        LOGE("tcsetattr %s: %s", path.c_str(), strerror(errno));
        close(fd);
        return false;
    }
    tcflush(fd, TCIOFLUSH);
    fd_ = fd;
    return true;
}

bool SerialLink::open_tcp(const std::string& target) {
    const size_t colon = target.rfind(':');
    if (colon == std::string::npos) {
        LOGE("--device tcp:HOST:PORTA inválido: %s", target.c_str());
        return false;
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(target.substr(0, colon).c_str(), target.substr(colon + 1).c_str(), &hints, &res) != 0) return false;

    const int fd = socket(res->ai_family, res->ai_socktype | SOCK_CLOEXEC, res->ai_protocol);
    const bool ok = fd >= 0 && connect(fd, res->ai_addr, res->ai_addrlen) == 0;
    freeaddrinfo(res);
    if (!ok) {
        const std::string msg = "simulador indisponível em " + target + " (" + strerror(errno) + ")";
        if (msg != warned_) {
            LOGW("%s; tentando a cada %ld ms", msg.c_str(), kRetryMs);
            warned_ = msg;
        }
        if (fd >= 0) close(fd);
        return false;
    }
    const int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    fd_ = fd;
    return true;
}

bool SerialLink::maintain(long long now_ms) {
    if (fd_ < 0) {
        if (now_ms - last_try_ms_ < kRetryMs) return false;
        last_try_ms_ = now_ms;

        std::string path;
        bool tcp = false;
        if (!resolve(path, tcp) || !(tcp ? open_tcp(path) : open_tty(path))) return false;

        is_tcp_ = tcp;
        path_ = tcp ? "tcp:" + path : path;
        warned_.clear();
        ready_ = false;
        // Abrir a serial de um ESP32 DevKit pulsa DTR/RTS e o reinicia: espera o boot.
        ready_at_ms_ = now_ms + (tcp ? 0 : boot_wait_ms_);
        LOGI("conectado ao braço em %s%s", path_.c_str(),
             tcp || boot_wait_ms_ == 0 ? "" : " (aguardando o boot do ESP32)");
    }

    if (!ready_ && now_ms >= ready_at_ms_) {
        ready_ = true;
        return true;
    }
    return false;
}

long SerialLink::wait_hint_ms(long long now_ms) const {
    if (fd_ < 0) {
        const long long left = last_try_ms_ + kRetryMs - now_ms;
        return left > 0 ? long(left) : 0;
    }
    if (!ready_) {
        const long long left = ready_at_ms_ - now_ms;
        return left > 0 ? long(left) : 0;
    }
    return -1;
}

bool SerialLink::send(const uint8_t* data, size_t len) {
    if (fd_ < 0) return false;
    const ssize_t w = write(fd_, data, len);
    if (w == ssize_t(len)) return true;
    if (w < 0 && (errno == EAGAIN || errno == EINTR)) return false;  // buffer cheio: o forwarder tenta de novo
    close_link(w < 0 ? strerror(errno) : "escrita parcial");
    return false;
}

void SerialLink::on_readable(bool hangup) {
    char buf[256];
    for (;;) {
        const ssize_t r = read(fd_, buf, sizeof(buf));
        if (r > 0) {
            if (!echo_) continue;
            for (ssize_t i = 0; i < r; ++i) {
                if (buf[i] == '\n') {
                    if (!line_.empty()) LOGI("esp32: %s", line_.c_str());
                    line_.clear();
                } else if (buf[i] != '\r' && line_.size() < 200) {
                    line_ += buf[i];
                }
            }
            continue;
        }
        if (r == 0 && !is_tcp_ && !hangup) return;  // tty sem dados
        if (r < 0 && (errno == EAGAIN || errno == EINTR) && !hangup) return;
        // Cabo arrancado: POLLHUP + read 0 (tty desligada), EIO ou ENODEV.
        close_link(r == 0 ? (is_tcp_ ? "conexão fechada" : "serial desligada (hangup)") : strerror(errno));
        return;
    }
}

void SerialLink::close_link(const char* why) {
    if (fd_ < 0) return;
    if (why != nullptr) LOGW("link com o braço perdido (%s): %s", why, path_.c_str());
    close(fd_);
    fd_ = -1;
    ready_ = false;
    line_.clear();
}

}  // namespace snes_arm
