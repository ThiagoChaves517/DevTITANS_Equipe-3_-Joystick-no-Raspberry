#include <sys/socket.h>
#include <sys/un.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define SOCKET_PATH "/data/local/tmp/snes_uinput.sock"

static int send_state(int fd, uint16_t state)
{
    printf("[TEST] enviando 0x%04X\n", state);

    ssize_t result =
        write(
            fd,
            &state,
            sizeof(state)
        );

    if (result != sizeof(state))
    {
        perror("[TEST] write");
        return -1;
    }

    usleep(500000);

    return 0;
}

int main(void)
{
    int fd =
        socket(
            AF_UNIX,
            SOCK_STREAM,
            0
        );

    if (fd < 0)
    {
        perror("[TEST] socket");
        return 1;
    }

    struct sockaddr_un address;

    memset(
        &address,
        0,
        sizeof(address)
    );

    address.sun_family =
        AF_UNIX;

    strncpy(
        address.sun_path,
        SOCKET_PATH,
        sizeof(address.sun_path) - 1
    );

    if (connect(
            fd,
            (struct sockaddr*)&address,
            sizeof(address)
        ) < 0)
    {
        perror("[TEST] connect");
        close(fd);
        return 1;
    }

    printf("[TEST] conectado ao daemon\n");

    send_state(fd, 0xFFFF);

    send_state(fd, 0xFEFF);

    send_state(fd, 0xFFFE);

    send_state(fd, 0xFEF5);

    send_state(fd, 0xFCF5);

    send_state(fd, 0xFFFF);

    close(fd);

    printf("[TEST] teste finalizado\n");

    return 0;
}