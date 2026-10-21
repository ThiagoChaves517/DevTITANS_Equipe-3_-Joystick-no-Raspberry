#include "backend.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef __linux__

/*
 * Controle DIRETO dos servos pelos GPIOs da Raspberry (sem ESP32).
 *
 * A Pi 4 tem só 2 canais de PWM de hardware e eles estão desligados no
 * device tree do GloDroid (ligar exige mexer no config.txt). Então o pulso
 * de 50 Hz das 4 juntas é gerado por software: uma thread SCHED_FIFO sobe as
 * 4 linhas juntas a cada 20 ms e baixa cada uma no instante certo
 * (clock_nanosleep até ~150 µs antes + espera ativa no final).
 *
 * Limitações honestas (ver braco/README.md): o jitter depende da carga do
 * Android; o SG90 tem banda morta de ~10 µs, então tremidas pequenas podem
 * aparecer. Para produção o caminho recomendado é o backend serial (ESP32,
 * PWM de hardware LEDC).
 *
 * Usa a API de GPIO v2 do kernel (/dev/gpiochipN), sem sysfs.
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define PERIOD_NS     20000000L
#define SPIN_GUARD_NS   150000L
#define STATS_EVERY_MS    5000u

struct gpio_backend
{
    int chip_fd;
    int line_fd;
    int pins[JOINT_COUNT];
    float pulse_min_us[JOINT_COUNT];
    float pulse_max_us[JOINT_COUNT];

    atomic_int pulse_us[JOINT_COUNT]; /* 0 = sem pulso (linha fica em 0) */
    atomic_bool running;
    pthread_t thread;
    bool thread_started;
    bool realtime;

    bool stats;
    atomic_int err_max_ns;
    atomic_long err_sum_ns;
    atomic_int err_count;
    uint32_t last_stats_ms;
    char status[96];
};

static int64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void sleep_until_ns(int64_t deadline)
{
    struct timespec ts = { (time_t)(deadline / 1000000000LL), (long)(deadline % 1000000000LL) };

    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR)
        ;
}

static void set_lines(struct gpio_backend *b, uint64_t mask, uint64_t bits)
{
    struct gpio_v2_line_values values = { .bits = bits, .mask = mask };
    ioctl(b->line_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &values);
}

static void *pwm_thread(void *arg)
{
    struct gpio_backend *b = arg;
    int64_t period_start = now_ns();

    while (atomic_load(&b->running))
    {
        int pulse[JOINT_COUNT];
        int order[JOINT_COUNT];
        uint64_t active = 0;

        for (int j = 0; j < JOINT_COUNT; ++j)
        {
            pulse[j] = atomic_load(&b->pulse_us[j]);
            order[j] = j;

            if (pulse[j] > 0)
                active |= 1ull << j;
        }

        /* Ordena pela largura do pulso: as linhas descem em sequência. */
        for (int i = 1; i < JOINT_COUNT; ++i)
        {
            for (int k = i; k > 0 && pulse[order[k]] < pulse[order[k - 1]]; --k)
            {
                const int tmp = order[k];
                order[k] = order[k - 1];
                order[k - 1] = tmp;
            }
        }

        if (active)
        {
            set_lines(b, active, active);
            const int64_t rise = now_ns();

            for (int i = 0; i < JOINT_COUNT; ++i)
            {
                const int j = order[i];

                if (!(active & (1ull << j)))
                    continue;

                const int64_t fall = rise + (int64_t)pulse[j] * 1000;

                if (fall - now_ns() > SPIN_GUARD_NS)
                    sleep_until_ns(fall - SPIN_GUARD_NS);

                while (now_ns() < fall)
                    ;

                set_lines(b, 1ull << j, 0);

                const int err = (int)(now_ns() - fall);

                atomic_fetch_add(&b->err_sum_ns, err);
                atomic_fetch_add(&b->err_count, 1);

                if (err > atomic_load(&b->err_max_ns))
                    atomic_store(&b->err_max_ns, err);
            }
        }

        period_start += PERIOD_NS;

        /* Se atrasou mais de um período inteiro, realinha em vez de "correr atrás". */
        if (now_ns() > period_start)
            period_start = now_ns();

        sleep_until_ns(period_start);
    }

    return NULL;
}

static bool line_is_free(int chip_fd, int offset)
{
    struct gpio_v2_line_info info;

    memset(&info, 0, sizeof(info));
    info.offset = (uint32_t)offset;

    if (ioctl(chip_fd, GPIO_V2_GET_LINEINFO_IOCTL, &info) < 0)
    {
        printf("[GPIO] linha %d inválida: %s\n", offset, strerror(errno));
        return false;
    }

    if (info.flags & GPIO_V2_LINE_FLAG_USED)
    {
        printf("[GPIO] GPIO%d já está em uso por \"%s\"\n", offset, info.consumer);
        return false;
    }

    return true;
}

static void gpio_update(void *impl, const float target[JOINT_COUNT], uint32_t now_ms)
{
    struct gpio_backend *b = impl;

    for (int j = 0; j < JOINT_COUNT; ++j)
    {
        const float span = b->pulse_max_us[j] - b->pulse_min_us[j];
        const float us = b->pulse_min_us[j] + target[j] / 180.0f * span;

        atomic_store(&b->pulse_us[j], (int)(us + 0.5f));
    }

    if (!b->stats || (uint32_t)(now_ms - b->last_stats_ms) < STATS_EVERY_MS)
        return;

    b->last_stats_ms = now_ms;

    const int count = atomic_exchange(&b->err_count, 0);
    const long sum = atomic_exchange(&b->err_sum_ns, 0);
    const int max = atomic_exchange(&b->err_max_ns, 0);

    if (count > 0)
    {
        printf("[GPIO] jitter da borda de descida: média %.1f µs, pior %.1f µs (%d pulsos, %s)\n",
               (double)sum / count / 1000.0, max / 1000.0, count,
               b->realtime ? "SCHED_FIFO" : "sem tempo real");
    }
}

static void gpio_note(void *impl, const char *text)
{
    (void)impl;
    printf("[GPIO] # %s\n", text);
}

static const char *gpio_status(void *impl)
{
    struct gpio_backend *b = impl;

    snprintf(b->status, sizeof(b->status), "gpio: GPIO%d/%d/%d/%d %s",
             b->pins[0], b->pins[1], b->pins[2], b->pins[3],
             b->realtime ? "SCHED_FIFO" : "normal");
    return b->status;
}

static void gpio_close(void *impl)
{
    struct gpio_backend *b = impl;

    if (b->thread_started)
    {
        atomic_store(&b->running, false);
        pthread_join(b->thread, NULL);
    }

    if (b->line_fd >= 0)
    {
        /* Linhas em 0 e liberadas: os servos ficam sem pulso (soltos). */
        set_lines(b, (1ull << JOINT_COUNT) - 1, 0);
        close(b->line_fd);
    }

    if (b->chip_fd >= 0)
        close(b->chip_fd);

    printf("[GPIO] linhas liberadas\n");
    free(b);
}

static const struct backend_ops gpio_ops =
{
    gpio_update, gpio_note, NULL, gpio_status, gpio_close
};

void *backend_gpio_open(const struct backend_options *opt, const struct backend_ops **ops)
{
    struct gpio_backend *b = calloc(1, sizeof(*b));

    if (b == NULL)
        return NULL;

    b->chip_fd = -1;
    b->line_fd = -1;
    b->stats = opt->stats;

    const char *chip = opt->gpiochip ? opt->gpiochip : "/dev/gpiochip0";

    b->chip_fd = open(chip, O_RDWR | O_CLOEXEC);

    if (b->chip_fd < 0)
    {
        printf("[GPIO] não abriu %s: %s (rodar como root: adb root)\n", chip, strerror(errno));
        gpio_close(b);
        return NULL;
    }

    struct gpio_v2_line_request req;
    memset(&req, 0, sizeof(req));

    for (int j = 0; j < JOINT_COUNT; ++j)
    {
        b->pins[j] = opt->pins[j];
        b->pulse_min_us[j] = opt->pulse_min_us[j];
        b->pulse_max_us[j] = opt->pulse_max_us[j];

        if (!line_is_free(b->chip_fd, b->pins[j]))
        {
            gpio_close(b);
            return NULL;
        }

        req.offsets[j] = (uint32_t)b->pins[j];
    }

    req.num_lines = JOINT_COUNT;
    snprintf(req.consumer, sizeof(req.consumer), "snes_arm_bridge");
    req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    req.config.num_attrs = 1;
    req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    req.config.attrs[0].attr.values = 0;
    req.config.attrs[0].mask = (1ull << JOINT_COUNT) - 1;

    if (ioctl(b->chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0)
    {
        printf("[GPIO] não reservou as linhas: %s\n", strerror(errno));
        gpio_close(b);
        return NULL;
    }

    b->line_fd = req.fd;

    if (mlockall(MCL_CURRENT | MCL_FUTURE) < 0)
        printf("[GPIO] aviso: mlockall falhou (%s)\n", strerror(errno));

    atomic_store(&b->running, true);

    if (pthread_create(&b->thread, NULL, pwm_thread, b) != 0)
    {
        printf("[GPIO] não criou a thread de PWM\n");
        gpio_close(b);
        return NULL;
    }

    b->thread_started = true;

    struct sched_param sp = { .sched_priority = 80 };
    b->realtime = pthread_setschedparam(b->thread, SCHED_FIFO, &sp) == 0;

    if (!b->realtime)
        printf("[GPIO] aviso: sem SCHED_FIFO (rodar como root); o jitter vai ser maior\n");

    printf("[GPIO] PWM por software em %s: base=GPIO%d ombro=GPIO%d cotovelo=GPIO%d garra=GPIO%d\n",
           chip, b->pins[0], b->pins[1], b->pins[2], b->pins[3]);

    *ops = &gpio_ops;
    return b;
}

#else /* !__linux__ */

void *backend_gpio_open(const struct backend_options *opt, const struct backend_ops **ops)
{
    (void)opt;
    (void)ops;
    fprintf(stderr, "[GPIO] backend gpio só existe na Raspberry (Linux/Android)\n");
    return NULL;
}

#endif
