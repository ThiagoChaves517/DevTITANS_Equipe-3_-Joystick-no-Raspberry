#include "uinput_device.h"

#include <linux/uinput.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <initializer_list>
#include <cstring>

#include "log.h"

namespace snes {

// Os nomes em snes::code colidem com as macros do kernel, então a conferência usa valores já resolvidos.
static_assert(kButtons[0].code == BTN_B && kButtons[4].code == BTN_A && kButtons[7].code == BTN_TR &&
                  kGripOpenCode == BTN_0 && kGripCloseCode == BTN_1 && kHatXCode == ABS_HAT0X && kHatYCode == ABS_HAT0Y,
              "códigos replicados em controller_device.h divergem do kernel");

UinputDevice::~UinputDevice() { destroy(); }

bool UinputDevice::create(const char* path, const char* name) {
    fd_ = open(path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) {
        LOGE("open %s: %s (o usuário precisa do grupo uhid ou de root)", path, strerror(errno));
        return false;
    }
    auto io = [this](unsigned long req, long arg, const char* what) {
        if (ioctl(fd_, req, arg) < 0) { LOGE("%s: %s", what, strerror(errno)); return false; }
        return true;
    };
    bool ok = io(UI_SET_EVBIT, EV_KEY, "UI_SET_EVBIT EV_KEY") && io(UI_SET_EVBIT, EV_ABS, "UI_SET_EVBIT EV_ABS") &&
              io(UI_SET_EVBIT, EV_SYN, "UI_SET_EVBIT EV_SYN");
    for (const ButtonMap& m : kButtons) ok = ok && io(UI_SET_KEYBIT, m.code, "UI_SET_KEYBIT");
    ok = ok && io(UI_SET_KEYBIT, kGripOpenCode, "UI_SET_KEYBIT grip") && io(UI_SET_KEYBIT, kGripCloseCode, "UI_SET_KEYBIT grip");
    for (int axis : {ABS_HAT0X, ABS_HAT0Y}) {
        uinput_abs_setup abs{};
        abs.code = uint16_t(axis);
        abs.absinfo.minimum = -1;
        abs.absinfo.maximum = 1;
        ok = ok && io(UI_SET_ABSBIT, axis, "UI_SET_ABSBIT") && ioctl(fd_, UI_ABS_SETUP, &abs) >= 0;
    }
    if (ok) {
        uinput_setup setup{};
        setup.id.bustype = BUS_VIRTUAL;
        setup.id.vendor = 0x1209;   // pid.codes (VID para projetos abertos)
        setup.id.product = 0x534E;  // "SN"
        setup.id.version = 1;
        std::snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "%s", name);
        ok = ioctl(fd_, UI_DEV_SETUP, &setup) >= 0 && ioctl(fd_, UI_DEV_CREATE) >= 0;
        if (!ok) LOGE("UI_DEV_SETUP/UI_DEV_CREATE: %s", strerror(errno));
    }
    if (!ok) {
        close(fd_);
        fd_ = -1;
        return false;
    }
    LOGI("dispositivo uinput criado: \"%s\" (1209:534E)", name);
    return true;
}

void UinputDevice::destroy() {
    if (fd_ < 0) return;
    ioctl(fd_, UI_DEV_DESTROY);
    close(fd_);
    fd_ = -1;
    LOGI("dispositivo uinput destruído");
}

bool UinputDevice::emit(int type, int c, int value) {
    input_event ev{};
    ev.type = uint16_t(type);
    ev.code = uint16_t(c);
    ev.value = value;
    if (write(fd_, &ev, sizeof(ev)) != ssize_t(sizeof(ev))) {
        LOGE("write input_event type=%d code=%d: %s", type, c, strerror(errno));
        return false;
    }
    return true;
}

bool UinputDevice::sync() { return emit(EV_SYN, SYN_REPORT, 0); }

}  // namespace snes
