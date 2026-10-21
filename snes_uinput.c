// snes_uinput.c
#include <linux/uinput.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

static void emit(int fd, int type, int code, int val) {
    struct input_event ev = {0};
    ev.type = type; ev.code = code; ev.value = val;
    write(fd, &ev, sizeof ev);
}

int main(void) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    int btns[] = {BTN_A, BTN_B, BTN_X, BTN_Y, BTN_TL, BTN_TR, BTN_SELECT, BTN_START};
    for (int i = 0; i < 8; i++) ioctl(fd, UI_SET_KEYBIT, btns[i]);

    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    int axes[] = {ABS_HAT0X, ABS_HAT0Y};
    for (int i = 0; i < 2; i++) {
        ioctl(fd, UI_SET_ABSBIT, axes[i]);
        struct uinput_abs_setup as = {0};
        as.code = axes[i]; as.absinfo.minimum = -1; as.absinfo.maximum = 1;
        ioctl(fd, UI_ABS_SETUP, &as);
    }

    struct uinput_setup us = {0};
    us.id.bustype = BUS_VIRTUAL; us.id.vendor = 0x1234; us.id.product = 0x0001;
    strcpy(us.name, "SNES Joystick");
    ioctl(fd, UI_DEV_SETUP, &us);
    ioctl(fd, UI_DEV_CREATE);

    for (;;) {                       // teste: aperta e solta o botão A a cada segundo
        emit(fd, EV_KEY, BTN_A, 1); emit(fd, EV_SYN, SYN_REPORT, 0); sleep(1);
        emit(fd, EV_KEY, BTN_A, 0); emit(fd, EV_SYN, SYN_REPORT, 0); sleep(1);
    }
}
