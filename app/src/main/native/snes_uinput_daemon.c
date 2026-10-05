#include <linux/uinput.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SOCKET_PATH "/data/local/tmp/snes_uinput.sock"

static int uinput_fd = -1;
static int server_fd = -1;

static volatile sig_atomic_t running = 1;

static uint16_t previous_state = 0xFFFF;

static void handle_signal(int signal)
{
    (void)signal;
    running = 0;
}

static void emit_event(int type, int code, int value)
{
    struct input_event ev;

    memset(&ev, 0, sizeof(ev));

    ev.type = type;
    ev.code = code;
    ev.value = value;

    if (write(uinput_fd, &ev, sizeof(ev)) < 0)
    {
        perror("[UINPUT] write input_event");
    }
}

static void sync_report(void)
{
    emit_event(EV_SYN, SYN_REPORT, 0);
}

/*
 * Cria o dispositivo virtual de controle usando uinput.
 */
static int create_uinput(void)
{
    uinput_fd =
        open("/dev/uinput", O_WRONLY | O_NONBLOCK);

    if (uinput_fd < 0)
    {
        perror("[UINPUT] open /dev/uinput");
        return -1;
    }

    printf("[UINPUT] /dev/uinput aberto\n");

    /*
     * =========================
     * Botões
     * =========================
     */

    if (ioctl(
            uinput_fd,
            UI_SET_EVBIT,
            EV_KEY) < 0)
    {
        perror("[UINPUT] UI_SET_EVBIT EV_KEY");
        goto error;
    }

    const int buttons[] =
    {
        BTN_A,
        BTN_B,
        BTN_X,
        BTN_Y,
        BTN_TL,
        BTN_TR,
        BTN_SELECT,
        BTN_START
    };

    const size_t button_count =
        sizeof(buttons) / sizeof(buttons[0]);

    for (size_t i = 0; i < button_count; ++i)
    {
        if (ioctl(
                uinput_fd,
                UI_SET_KEYBIT,
                buttons[i]) < 0)
        {
            perror("[UINPUT] UI_SET_KEYBIT");
            goto error;
        }
    }

    /*
     * =========================
     * D-Pad
     * =========================
     */

    if (ioctl(
            uinput_fd,
            UI_SET_EVBIT,
            EV_ABS) < 0)
    {
        perror("[UINPUT] UI_SET_EVBIT EV_ABS");
        goto error;
    }

    const int axes[] =
    {
        ABS_HAT0X,
        ABS_HAT0Y
    };

    const size_t axis_count =
        sizeof(axes) / sizeof(axes[0]);

    for (size_t i = 0; i < axis_count; ++i)
    {
        if (ioctl(
                uinput_fd,
                UI_SET_ABSBIT,
                axes[i]) < 0)
        {
            perror("[UINPUT] UI_SET_ABSBIT");
            goto error;
        }

        struct uinput_abs_setup abs_setup;

        memset(
            &abs_setup,
            0,
            sizeof(abs_setup));

        abs_setup.code = axes[i];

        abs_setup.absinfo.minimum = -1;
        abs_setup.absinfo.maximum = 1;
        abs_setup.absinfo.flat = 0;
        abs_setup.absinfo.fuzz = 0;

        if (ioctl(
                uinput_fd,
                UI_ABS_SETUP,
                &abs_setup) < 0)
        {
            perror("[UINPUT] UI_ABS_SETUP");
            goto error;
        }
    }

    /*
     * =========================
     * Identidade do dispositivo
     * =========================
     */

    struct uinput_setup setup;

    memset(
        &setup,
        0,
        sizeof(setup));

    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor = 0x1234;
    setup.id.product = 0x0001;
    setup.id.version = 1;

    strncpy(
        setup.name,
        "SNES-BT-Controller",
        UINPUT_MAX_NAME_SIZE - 1);

    if (ioctl(
            uinput_fd,
            UI_DEV_SETUP,
            &setup) < 0)
    {
        perror("[UINPUT] UI_DEV_SETUP");
        goto error;
    }

    if (ioctl(
            uinput_fd,
            UI_DEV_CREATE) < 0)
    {
        perror("[UINPUT] UI_DEV_CREATE");
        goto error;
    }

    /*
     * Dá tempo para o kernel registrar
     * o novo dispositivo de input.
     */
    usleep(100000);

    previous_state = 0xFFFF;

    printf(
        "[UINPUT] dispositivo criado: "
        "SNES-BT-Controller\n");

    fflush(stdout);

    return 0;

error:

    close(uinput_fd);

    uinput_fd = -1;

    return -1;
}

static void destroy_uinput(void)
{
    if (uinput_fd < 0)
        return;

    ioctl(
        uinput_fd,
        UI_DEV_DESTROY);

    close(uinput_fd);

    uinput_fd = -1;

    printf(
        "[UINPUT] dispositivo destruido\n");
}

/*
 * Atualiza um botão somente quando seu estado mudou.
 *
 * O protocolo SNES é ativo em LOW:
 *
 * 0 = pressionado
 * 1 = solto
 */
static void update_button(
    uint16_t state,
    uint16_t previous,
    uint16_t mask,
    int key_code)
{
    const int current_pressed =
        ((state & mask) == 0);

    const int previous_pressed =
        ((previous & mask) == 0);

    if (current_pressed == previous_pressed)
        return;

    emit_event(
        EV_KEY,
        key_code,
        current_pressed ? 1 : 0);
}

/*
 * Converte:
 *
 * LEFT  -> -1
 * RIGHT -> +1
 * nenhum -> 0
 */
static int get_hat_x(uint16_t state)
{
    const int left =
        ((state & (1u << 6)) == 0);

    const int right =
        ((state & (1u << 7)) == 0);

    if (left && !right)
        return -1;

    if (right && !left)
        return 1;

    return 0;
}

/*
 * Converte:
 *
 * UP   -> -1
 * DOWN -> +1
 * nenhum -> 0
 */
static int get_hat_y(uint16_t state)
{
    const int up =
        ((state & (1u << 4)) == 0);

    const int down =
        ((state & (1u << 5)) == 0);

    if (up && !down)
        return -1;

    if (down && !up)
        return 1;

    return 0;
}

static void update_dpad(
    uint16_t state,
    uint16_t previous)
{
    const int x =
        get_hat_x(state);

    const int y =
        get_hat_y(state);

    const int previous_x =
        get_hat_x(previous);

    const int previous_y =
        get_hat_y(previous);

    if (x != previous_x)
    {
        emit_event(
            EV_ABS,
            ABS_HAT0X,
            x);
    }

    if (y != previous_y)
    {
        emit_event(
            EV_ABS,
            ABS_HAT0Y,
            y);
    }
}

/*
 * Recebe o estado SNES de 16 bits
 * e transforma em eventos Linux.
 *
 * Mapeamento:
 *
 * bit 0  B      -> BTN_B
 * bit 1  Y      -> BTN_Y
 * bit 2  SELECT -> BTN_SELECT
 * bit 3  START  -> BTN_START
 *
 * bit 4  UP
 * bit 5  DOWN
 * bit 6  LEFT
 * bit 7  RIGHT
 *
 * bit 8  A      -> BTN_A
 * bit 9  X      -> BTN_X
 * bit 10 L      -> BTN_TL
 * bit 11 R      -> BTN_TR
 */
static void update_state(uint16_t state)
{
    const uint16_t previous =
        previous_state;

    /*
     * B
     */
    update_button(
        state,
        previous,
        (1u << 0),
        BTN_B);

    /*
     * Y
     */
    update_button(
        state,
        previous,
        (1u << 1),
        BTN_Y);

    /*
     * SELECT
     */
    update_button(
        state,
        previous,
        (1u << 2),
        BTN_SELECT);

    /*
     * START
     */
    update_button(
        state,
        previous,
        (1u << 3),
        BTN_START);

    /*
     * D-Pad
     */
    update_dpad(
        state,
        previous);

    /*
     * A
     */
    update_button(
        state,
        previous,
        (1u << 8),
        BTN_A);

    /*
     * X
     */
    update_button(
        state,
        previous,
        (1u << 9),
        BTN_X);

    /*
     * L
     */
    update_button(
        state,
        previous,
        (1u << 10),
        BTN_TL);

    /*
     * R
     */
    update_button(
        state,
        previous,
        (1u << 11),
        BTN_TR);

    /*
     * Só enviamos SYN_REPORT quando
     * alguma coisa mudou.
     */
    if (state != previous)
    {
        sync_report();

        printf(
            "[STATE] 0x%04X\n",
            state);

        fflush(stdout);
    }

    previous_state = state;
}

/*
 * Cria o Unix Domain Socket.
 *
 * O Android APK não terá acesso ao
 * /dev/uinput, mas poderá conversar
 * com este daemon através do socket.
 */
static int create_socket(void)
{
    unlink(SOCKET_PATH);

    server_fd =
        socket(
            AF_UNIX,
            SOCK_STREAM,
            0);

    if (server_fd < 0)
    {
        perror("[SOCKET] socket");
        return -1;
    }

    struct sockaddr_un address;

    memset(
        &address,
        0,
        sizeof(address));

    address.sun_family =
        AF_UNIX;

    strncpy(
        address.sun_path,
        SOCKET_PATH,
        sizeof(address.sun_path) - 1);

    if (bind(
            server_fd,
            (struct sockaddr*)&address,
            sizeof(address)) < 0)
    {
        perror("[SOCKET] bind");

        close(server_fd);

        server_fd = -1;

        return -1;
    }

    /*
     * Permite que o processo do APK
     * consiga conectar ao socket.
     */
    if (chmod(
            SOCKET_PATH,
            0666) < 0)
    {
        perror("[SOCKET] chmod");
        goto error;
    }

    if (listen(
            server_fd,
            1) < 0)
    {
        perror("[SOCKET] listen");
        goto error;
    }

    printf(
        "[SOCKET] aguardando em %s\n",
        SOCKET_PATH);

    fflush(stdout);

    return 0;

error:

    close(server_fd);

    server_fd = -1;

    unlink(SOCKET_PATH);

    return -1;
}

/*
 * Processa uma conexão do Android.
 */
static void process_client(int client_fd)
{
    printf(
        "[SOCKET] cliente conectado\n");

    fflush(stdout);

    while (running)
    {
        uint16_t state;

        const ssize_t received =
            read(
                client_fd,
                &state,
                sizeof(state));

        if (received == 0)
        {
            printf(
                "[SOCKET] cliente desconectado\n");

            fflush(stdout);

            break;
        }

        if (received < 0)
        {
            if (errno == EINTR)
                continue;

            perror(
                "[SOCKET] read");

            break;
        }

        if (received != sizeof(state))
        {
            printf(
                "[SOCKET] pacote invalido: "
                "%zd bytes\n",
                received);

            fflush(stdout);

            break;
        }

        update_state(state);
    }
}

int main(void)
{
    signal(
        SIGINT,
        handle_signal);

    signal(
        SIGTERM,
        handle_signal);

    printf(
        "==============================\n");

    printf(
        " SNES uinput daemon\n");

    printf(
        "==============================\n");

    fflush(stdout);

    /*
     * 1. Cria o dispositivo virtual.
     */
    if (create_uinput() < 0)
    {
        fprintf(
            stderr,
            "[ERROR] Falha ao criar uinput\n");

        return 1;
    }

    /*
     * 2. Cria o socket IPC.
     */
    if (create_socket() < 0)
    {
        fprintf(
            stderr,
            "[ERROR] Falha ao criar socket\n");

        destroy_uinput();

        return 1;
    }

    /*
     * 3. Aguarda o Android.
     */
    while (running)
    {
        const int client_fd =
            accept(
                server_fd,
                NULL,
                NULL);

        if (client_fd < 0)
        {
            if (errno == EINTR)
                continue;

            perror(
                "[SOCKET] accept");

            break;
        }

        process_client(
            client_fd);

        close(client_fd);
    }

    /*
     * 4. Cleanup.
     */
    close(server_fd);

    server_fd = -1;

    unlink(SOCKET_PATH);

    destroy_uinput();

    return 0;
} 