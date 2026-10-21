/*
 * snes_arm_bridge — integração Raspberry Pi -> braço robótico.
 *
 *   joystick -> (BLE/APK/snes_uinput_daemon) -> /dev/input/eventX
 *            -> [esta bridge: mapeamento + alvo das juntas a 50 Hz]
 *            -> ESP32 do braço (serial) | GPIO direto | simulador (tcp)
 *
 * Roda em /data/local/tmp como o daemon do joystick. Ver braco/README.md.
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "arm_model.h"
#include "backend.h"
#include "input.h"
#include "mapping.h"

static volatile sig_atomic_t running = 1;

static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

static uint32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000);
}

static void sleep_ms_until(uint32_t deadline_ms)
{
    const int32_t wait = (int32_t)(deadline_ms - now_ms());

    if (wait <= 0)
        return;

    struct timespec ts = { wait / 1000, (wait % 1000) * 1000000L };

    while (nanosleep(&ts, &ts) < 0 && errno == EINTR && running)
        ;
}

static void usage(const char *argv0)
{
    printf(
        "uso: %s [opções]\n"
        "  --backend serial|gpio|tcp|none  saída (padrão: serial = ESP32 do braço)\n"
        "  --device auto|/dev/ttyUSB0      serial do ESP32 (padrão: auto)\n"
        "  --host H --port P               simulador para --backend tcp (127.0.0.1:7878)\n"
        "  --pins B,O,C,G                  GPIOs para --backend gpio (padrão 17,27,22,23)\n"
        "  --gpiochip /dev/gpiochip0       chip de GPIO\n"
        "  --pulse MIN,MAX                 µs para 0° e 180° (padrão 500,2400)\n"
        "  --input auto|/dev/input/eventN|stdin\n"
        "  --input-name NOME               nome do gamepad (padrão SNES-BT-Controller)\n"
        "  --rate HZ                       frequência do laço (padrão 50)\n"
        "  --duration-ms N                 encerra sozinho depois de N ms (testes)\n"
        "  --stats                         mede o jitter do PWM (backend gpio)\n"
        "  --verbose                       mostra o tráfego com o braço\n",
        argv0);
}

static bool parse_ints(const char *text, int *out, int count)
{
    char *end;

    for (int i = 0; i < count; ++i)
    {
        out[i] = (int)strtol(text, &end, 10);

        if (end == text)
            return false;

        text = (*end == ',') ? end + 1 : end;
    }

    return true;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    const char *backend_kind = "serial";
    const char *input_spec = "auto";
    const char *input_name = "SNES-BT-Controller";
    int rate_hz = 50;
    long duration_ms = 0;

    struct backend_options opt;
    memset(&opt, 0, sizeof(opt));
    opt.device = "auto";
    opt.host = "127.0.0.1";
    opt.port = 7878;
    opt.gpiochip = "/dev/gpiochip0";

    const int default_pins[JOINT_COUNT] = { 17, 27, 22, 23 };
    memcpy(opt.pins, default_pins, sizeof(default_pins));

    int pulse[2] = { 500, 2400 };

    for (int i = 1; i < argc; ++i)
    {
        const char *arg = argv[i];
        const char *val = (i + 1 < argc) ? argv[i + 1] : NULL;
        bool used_value = true;

        if (strcmp(arg, "--backend") == 0 && val)            backend_kind = val;
        else if (strcmp(arg, "--device") == 0 && val)        opt.device = val;
        else if (strcmp(arg, "--host") == 0 && val)          opt.host = val;
        else if (strcmp(arg, "--port") == 0 && val)          opt.port = atoi(val);
        else if (strcmp(arg, "--gpiochip") == 0 && val)      opt.gpiochip = val;
        else if (strcmp(arg, "--input") == 0 && val)         input_spec = val;
        else if (strcmp(arg, "--input-name") == 0 && val)    input_name = val;
        else if (strcmp(arg, "--rate") == 0 && val)          rate_hz = atoi(val);
        else if (strcmp(arg, "--duration-ms") == 0 && val)   duration_ms = atol(val);
        else if (strcmp(arg, "--pins") == 0 && val)
        {
            if (!parse_ints(val, opt.pins, JOINT_COUNT))
            {
                fprintf(stderr, "--pins espera 4 números: base,ombro,cotovelo,garra\n");
                return 2;
            }
        }
        else if (strcmp(arg, "--pulse") == 0 && val)
        {
            if (!parse_ints(val, pulse, 2) || pulse[0] >= pulse[1])
            {
                fprintf(stderr, "--pulse espera MIN,MAX em µs (ex.: 500,2400)\n");
                return 2;
            }
        }
        else
        {
            used_value = false;

            if (strcmp(arg, "--stats") == 0)        opt.stats = true;
            else if (strcmp(arg, "--verbose") == 0) opt.verbose = true;
            else
            {
                usage(argv[0]);
                return strcmp(arg, "--help") == 0 ? 0 : 2;
            }
        }

        if (used_value)
            ++i;
    }

    if (rate_hz < 10 || rate_hz > 200)
    {
        fprintf(stderr, "--rate deve ficar entre 10 e 200 Hz\n");
        return 2;
    }

    for (int j = 0; j < JOINT_COUNT; ++j)
    {
        opt.pulse_min_us[j] = (float)pulse[0];
        opt.pulse_max_us[j] = (float)pulse[1];
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    printf("==============================\n");
    printf(" SNES arm bridge\n");
    printf("==============================\n");

    struct joint_config cfg[JOINT_COUNT];
    arm_default_config(cfg);

    struct arm_model arm;
    arm_init(&arm, cfg);

    struct mapper mapper;
    mapper_init(&mapper);

    struct input_source *input = input_open(input_spec, input_name);

    if (input == NULL)
        return 1;

    struct backend *backend = backend_open(backend_kind, &opt);

    if (backend == NULL)
    {
        input_close(input);
        return 1;
    }

    printf("[BRIDGE] backend=%s, %d Hz, página %s\n",
           backend_kind, rate_hz, mapper_page_name(mapper.page));

    const uint32_t tick_ms = 1000u / (uint32_t)rate_hz;
    const uint32_t start_ms = now_ms();
    uint32_t next_tick = start_ms;
    uint32_t last_tick = start_ms;
    uint32_t last_log_ms = 0;
    bool was_connected = false;
    bool was_ready = false;
    int logged_target[JOINT_COUNT] = { -1, -1, -1, -1 };

    while (running)
    {
        const uint32_t now = now_ms();
        const float dt = (float)(now - last_tick) / 1000.0f;
        last_tick = now;

        if (duration_ms > 0 && (long)(now - start_ms) >= duration_ms)
            break;

        struct pad_state pad;
        input_poll(input, &pad, now);

        if (pad.connected != was_connected)
        {
            printf("[BRIDGE] controle %s\n", pad.connected ? "ativo" : "ausente: braço parado");
            was_connected = pad.connected;
        }

        struct mapper_output out;
        mapper_update(&mapper, &pad, now, &out);

        if (out.events & MAPPER_EVENT_PAGE_CHANGED)
        {
            char note[48];
            snprintf(note, sizeof(note), "PAGINA %d %s", mapper.page, mapper_page_name(mapper.page));
            printf("[BRIDGE] START: página %d (%s)\n", mapper.page, mapper_page_name(mapper.page));
            backend_note(backend, note);
        }

        if (out.events & MAPPER_EVENT_HOME)
        {
            printf("[BRIDGE] START segurado: voltando para home\n");
            arm_start_homing(&arm);
            backend_note(backend, "HOME");
        }

        struct backend_limits limits;

        if (backend_take_limits(backend, &limits))
        {
            for (int j = 0; j < JOINT_COUNT; ++j)
            {
                arm_set_limits(&arm, j, limits.min[j], limits.max[j], limits.home[j]);
                printf("[BRIDGE] limites do firmware: %-8s %5.1f..%5.1f home %5.1f\n",
                       arm_joint_name[j], limits.min[j], limits.max[j], limits.home[j]);
            }
        }

        const bool ready = backend_ready(backend);

        if (ready != was_ready)
        {
            printf("[BRIDGE] braço %s\n", ready ? "pronto: comandos liberados" : "indisponível: comandos ignorados");
            was_ready = ready;
        }

        if (!ready)
            memset(out.dir, 0, sizeof(out.dir));

        float real[JOINT_COUNT];

        if (backend_take_sync(backend, real))
        {
            for (int j = 0; j < JOINT_COUNT; ++j)
                arm.target[j] = real[j];

            arm.homing = false;
            printf("[BRIDGE] alvo sincronizado com a posição real: %.0f %.0f %.0f %.0f\n",
                   real[0], real[1], real[2], real[3]);
        }

        arm_step(&arm, out.dir, dt > 0.2f ? 0.2f : dt);
        backend_update(backend, arm.target, now);

        /* Log do alvo quando muda (no máximo 5x por segundo). */
        bool changed = false;

        for (int j = 0; j < JOINT_COUNT; ++j)
            changed |= (int)(arm.target[j] + 0.5f) != logged_target[j];

        if (changed && (uint32_t)(now - last_log_ms) >= 200u)
        {
            float tel[JOINT_COUNT];
            char tel_text[64] = "";

            if (backend_telemetry(backend, tel))
                snprintf(tel_text, sizeof(tel_text), " | real %.0f %.0f %.0f %.0f",
                         tel[0], tel[1], tel[2], tel[3]);

            for (int j = 0; j < JOINT_COUNT; ++j)
                logged_target[j] = (int)(arm.target[j] + 0.5f);

            printf("[ARM] alvo base=%d ombro=%d cotovelo=%d garra=%d%s%s\n",
                   logged_target[0], logged_target[1], logged_target[2], logged_target[3],
                   arm.homing ? " (home)" : "", tel_text);

            last_log_ms = now;
        }

        next_tick += tick_ms;

        if ((int32_t)(now_ms() - next_tick) > (int32_t)(5 * tick_ms))
            next_tick = now_ms(); /* ficou muito atrasado (ex.: suspensão): realinha */

        sleep_ms_until(next_tick);
    }

    printf("[BRIDGE] encerrando (%s)\n", backend_status(backend));

    backend_close(backend);
    input_close(input);
    return 0;
}
