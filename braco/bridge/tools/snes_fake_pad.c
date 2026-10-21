/*
 * snes_fake_pad — controle SNES falso para testar a bridge sem o joystick.
 *
 * Cria pelo /dev/uinput um gamepad com o MESMO nome e as mesmas capacidades
 * do snes_uinput_daemon (branch driver): BTN_A/B/X/Y/TL/TR/SELECT/START +
 * ABS_HAT0X/Y. Para a bridge (e para o Android) ele é igual ao real.
 *
 * Comandos pelo stdin, um por linha:
 *   press A | release A          aperta / solta (A B X Y L R SELECT START)
 *   tap A [ms]                   aperta, espera (padrão 100 ms) e solta
 *   hold A ms                    aperta por ms e solta
 *   hat x|y -1|0|1               D-pad
 *   state 0xFEFF                 estado SNES cru de 16 bits (0 = pressionado)
 *   sleep ms                     espera
 *   quit
 *
 * Exemplo:  adb shell /data/local/tmp/snes_fake_pad < roteiro.txt
 * Precisa de root (adb root): /dev/uinput é 0660 uhid.
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int fd = -1;

static const struct { const char *name; int code; int snes_bit; } buttons[] =
{
    { "B", BTN_B, 0 }, { "Y", BTN_Y, 1 }, { "SELECT", BTN_SELECT, 2 }, { "START", BTN_START, 3 },
    { "A", BTN_A, 8 }, { "X", BTN_X, 9 }, { "L", BTN_TL, 10 }, { "R", BTN_TR, 11 }
};

#define BUTTON_COUNT (sizeof(buttons) / sizeof(buttons[0]))

static void emit(int type, int code, int value)
{
    struct input_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = (uint16_t)type;
    ev.code = (uint16_t)code;
    ev.value = value;

    if (write(fd, &ev, sizeof(ev)) < 0)
        perror("[FAKE] write");
}

static void sync_report(void)
{
    emit(EV_SYN, SYN_REPORT, 0);
}

static int find_button(const char *name)
{
    for (size_t i = 0; i < BUTTON_COUNT; ++i)
    {
        if (strcmp(buttons[i].name, name) == 0)
            return (int)i;
    }

    printf("[FAKE] botão desconhecido: %s\n", name);
    return -1;
}

static void set_button(int index, int pressed)
{
    emit(EV_KEY, buttons[index].code, pressed);
    sync_report();
    printf("[FAKE] %s %s\n", buttons[index].name, pressed ? "pressionado" : "solto");
}

static void sleep_ms(long ms)
{
    usleep((useconds_t)ms * 1000u);
}

/* Mesmo tratamento do snes_uinput_daemon: bits 4-7 viram o D-pad. */
static void apply_snes_state(uint16_t state)
{
    for (size_t i = 0; i < BUTTON_COUNT; ++i)
        emit(EV_KEY, buttons[i].code, (state & (1u << buttons[i].snes_bit)) == 0);

    const int up = (state & (1u << 4)) == 0, down = (state & (1u << 5)) == 0;
    const int left = (state & (1u << 6)) == 0, right = (state & (1u << 7)) == 0;

    emit(EV_ABS, ABS_HAT0X, left && !right ? -1 : (right && !left ? 1 : 0));
    emit(EV_ABS, ABS_HAT0Y, up && !down ? -1 : (down && !up ? 1 : 0));
    sync_report();

    printf("[FAKE] estado 0x%04X\n", state);
}

static int create_device(const char *name)
{
    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);

    if (fd < 0)
    {
        perror("[FAKE] open /dev/uinput (rodou adb root?)");
        return -1;
    }

    ioctl(fd, UI_SET_EVBIT, EV_KEY);

    for (size_t i = 0; i < BUTTON_COUNT; ++i)
        ioctl(fd, UI_SET_KEYBIT, buttons[i].code);

    ioctl(fd, UI_SET_EVBIT, EV_ABS);

    const int axes[] = { ABS_HAT0X, ABS_HAT0Y };

    for (size_t i = 0; i < 2; ++i)
    {
        struct uinput_abs_setup abs;

        memset(&abs, 0, sizeof(abs));
        ioctl(fd, UI_SET_ABSBIT, axes[i]);
        abs.code = (uint16_t)axes[i];
        abs.absinfo.minimum = -1;
        abs.absinfo.maximum = 1;
        ioctl(fd, UI_ABS_SETUP, &abs);
    }

    struct uinput_setup setup;

    memset(&setup, 0, sizeof(setup));
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor = 0x1234;
    setup.id.product = 0x0002; /* o daemon real usa 0x0001 */
    setup.id.version = 1;
    strncpy(setup.name, name, UINPUT_MAX_NAME_SIZE - 1);

    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0)
    {
        perror("[FAKE] UI_DEV_CREATE");
        close(fd);
        return -1;
    }

    usleep(200000); /* tempo para o kernel/Android registrarem o dispositivo */
    printf("[FAKE] dispositivo criado: \"%s\"\n", name);
    return 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    const char *name = (argc > 2 && strcmp(argv[1], "--name") == 0) ? argv[2] : "SNES-BT-Controller";

    if (create_device(name) < 0)
        return 1;

    char line[128];

    while (fgets(line, sizeof(line), stdin) != NULL)
    {
        char *cmd = strtok(line, " \t\r\n");

        if (cmd == NULL || cmd[0] == '#')
            continue;

        char *a1 = strtok(NULL, " \t\r\n");
        char *a2 = strtok(NULL, " \t\r\n");
        int index;

        if (strcmp(cmd, "quit") == 0)
            break;
        else if (strcmp(cmd, "sleep") == 0 && a1)
            sleep_ms(atol(a1));
        else if ((strcmp(cmd, "press") == 0 || strcmp(cmd, "release") == 0) && a1)
        {
            if ((index = find_button(a1)) >= 0)
                set_button(index, cmd[0] == 'p');
        }
        else if ((strcmp(cmd, "tap") == 0 || strcmp(cmd, "hold") == 0) && a1)
        {
            if ((index = find_button(a1)) >= 0)
            {
                set_button(index, 1);
                sleep_ms(a2 ? atol(a2) : 100);
                set_button(index, 0);
            }
        }
        else if (strcmp(cmd, "hat") == 0 && a1 && a2)
        {
            emit(EV_ABS, a1[0] == 'x' ? ABS_HAT0X : ABS_HAT0Y, atoi(a2));
            sync_report();
            printf("[FAKE] hat %s = %s\n", a1, a2);
        }
        else if (strcmp(cmd, "state") == 0 && a1)
            apply_snes_state((uint16_t)strtoul(a1, NULL, 0));
        else
            printf("[FAKE] comando inválido: %s\n", cmd);
    }

    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    printf("[FAKE] dispositivo removido\n");
    return 0;
}
