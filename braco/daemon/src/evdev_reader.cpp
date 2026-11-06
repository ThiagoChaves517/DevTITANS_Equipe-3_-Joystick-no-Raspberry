#include "evdev_reader.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "log.h"

namespace snes_arm {

// Os nomes em snes::code colidem com as macros do kernel, então a conferência usa valores já resolvidos.
static_assert(kKeyBits[0].code == BTN_B && kKeyBits[4].code == BTN_A && kKeyBits[7].code == BTN_TR &&
                  kHatXCode == ABS_HAT0X && kHatYCode == ABS_HAT0Y && kSynDroppedCode == SYN_DROPPED,
              "códigos replicados em snes_register.h divergem do kernel");

namespace {

constexpr long kRescanMs = 1000;
constexpr size_t kBitsPerLong = sizeof(long) * 8;

bool test_bit(const unsigned long* bits, int bit) { return (bits[bit / kBitsPerLong] >> (bit % kBitsPerLong)) & 1ul; }

}  // namespace

void EvdevReader::resync(SnesRegister& reg) {
    unsigned long keys[(KEY_CNT + kBitsPerLong - 1) / kBitsPerLong] = {};
    uint16_t state = kAllReleased;

    if (ioctl(fd_, EVIOCGKEY(sizeof(keys)), keys) >= 0) {
        for (const KeyBit& k : kKeyBits)
            if (test_bit(keys, k.code)) state = uint16_t(state & ~(1u << k.bit));
    }

    input_absinfo abs{};
    if (ioctl(fd_, EVIOCGABS(ABS_HAT0X), &abs) >= 0) state = SnesRegister::with_hat_x(state, abs.value);
    if (ioctl(fd_, EVIOCGABS(ABS_HAT0Y), &abs) >= 0) state = SnesRegister::with_hat_y(state, abs.value);

    reg.set(state);
}

bool EvdevReader::try_open(const std::string& path, bool check_name, SnesRegister& reg) {
    const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return false;

    char name[128] = "";
    ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
    if (check_name && name_ != name) {
        close(fd);
        return false;
    }

    input_id id{};
    ioctl(fd, EVIOCGID, &id);

    fd_ = fd;
    path_ = path;
    resync(reg);
    warned_ = false;
    LOGI("controle encontrado: %s \"%s\" (%04x:%04x), estado 0x%04X", path.c_str(), name, id.vendor, id.product,
         reg.value());
    return true;
}

bool EvdevReader::maintain(long long now_ms, SnesRegister& reg) {
    if (fd_ >= 0 || now_ms - last_scan_ms_ < kRescanMs) return false;
    last_scan_ms_ = now_ms;

    if (!fixed_path_.empty()) {
        if (try_open(fixed_path_, false, reg)) return true;
    } else if (DIR* dir = opendir("/dev/input")) {
        bool found = false;
        while (dirent* e = readdir(dir)) {
            if (std::strncmp(e->d_name, "event", 5) != 0) continue;
            if ((found = try_open(std::string("/dev/input/") + e->d_name, true, reg))) break;
        }
        closedir(dir);
        if (found) return true;
    }

    if (!warned_) {
        LOGW("aguardando o controle \"%s\" (o snes_input_daemon está rodando?)",
             fixed_path_.empty() ? name_.c_str() : fixed_path_.c_str());
        warned_ = true;
    }
    return false;
}

int EvdevReader::read_events(SnesRegister& reg) {
    input_event ev[64];
    bool synced = false;
    bool dropped = false;

    for (;;) {
        const ssize_t n = read(fd_, ev, sizeof(ev));
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) break;
            close_device(strerror(errno));  // ENODEV: o dispositivo uinput foi destruído
            return -1;
        }
        if (n == 0) break;

        for (size_t i = 0; i < size_t(n) / sizeof(ev[0]); ++i) {
            if (ev[i].type == EV_SYN && ev[i].code == SYN_DROPPED) {
                dropped = true;  // fila do kernel estourou: relê tudo no fim
            } else if (ev[i].type == EV_SYN && ev[i].code == SYN_REPORT) {
                synced = true;
            } else {
                reg.apply(ev[i].type, ev[i].code, ev[i].value);
            }
        }
    }

    if (dropped) {
        resync(reg);
        return 1;
    }
    return synced ? 1 : 0;
}

long EvdevReader::wait_hint_ms(long long now_ms) const {
    if (fd_ >= 0) return -1;
    const long long left = last_scan_ms_ + kRescanMs - now_ms;
    return left > 0 ? long(left) : 0;
}

void EvdevReader::close_device(const char* why) {
    if (fd_ < 0) return;
    if (why != nullptr) LOGW("controle perdido (%s): %s", why, path_.c_str());
    close(fd_);
    fd_ = -1;
}

}  // namespace snes_arm
