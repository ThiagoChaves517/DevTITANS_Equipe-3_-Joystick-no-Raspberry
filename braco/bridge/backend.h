#pragma once

/*
 * Para onde vão os ângulos-alvo.
 *
 *   serial  ESP32 do braço via USB (CP2102 -> /dev/ttyUSB*, USB nativo -> /dev/ttyACM*)
 *   gpio    direto nos GPIOs da Raspberry, PWM gerado por software
 *   tcp     simulador (braco/simulador/arm_sim.py) via `adb reverse`
 *   none    só imprime (teste do mapeamento)
 *
 * Todos recebem update() a cada tick (50 Hz) com o alvo completo das 4 juntas.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arm_model.h"

struct backend_options
{
    const char *device;   /* serial: caminho ou "auto" */
    const char *host;     /* tcp */
    int         port;     /* tcp */
    int         pins[JOINT_COUNT]; /* gpio: offsets no gpiochip */
    const char *gpiochip; /* gpio */
    float       pulse_min_us[JOINT_COUNT];
    float       pulse_max_us[JOINT_COUNT];
    bool        stats;    /* gpio: imprime medição de jitter */
    bool        verbose;
};

struct backend_limits
{
    float min[JOINT_COUNT];
    float max[JOINT_COUNT];
    float home[JOINT_COUNT];
};

struct backend;

struct backend *backend_open(const char *kind, const struct backend_options *opt);

void backend_update(struct backend *b, const float target[JOINT_COUNT], uint32_t now_ms);

/* Comentário de diagnóstico ("# ..."): o firmware ignora, o simulador mostra. */
void backend_note(struct backend *b, const char *text);

/* true uma vez, quando o firmware informou limites novos (resposta ao comando I). */
bool backend_take_limits(struct backend *b, struct backend_limits *out);

/*
 * true quando o braço está ouvindo (serial/tcp: conectado e já respondeu
 * ou passou o tempo de boot). Enquanto for false a bridge não acumula
 * movimento: apertar um botão com o braço desligado não pode virar um
 * salto quando ele ligar.
 */
bool backend_ready(struct backend *b);

/*
 * true uma vez por conexão: posição real do braço (1ª telemetria). A bridge
 * adota esse valor como alvo para não dar tranco quando o ESP32 reinicia.
 */
bool backend_take_sync(struct backend *b, float out[JOINT_COUNT]);

/* Telemetria mais recente (ângulos realmente aplicados), se houver. */
bool backend_telemetry(struct backend *b, float out[JOINT_COUNT]);

const char *backend_status(struct backend *b);

/* Parada segura antes de sair: congela os servos. */
void backend_close(struct backend *b);

/* ---- protocolo de texto compartilhado por serial e tcp (link_text.c) ---- */

struct text_link
{
    const char *tag;
    bool     verbose;
    int      fd;
    char     rx[256];
    size_t   rx_len;
    uint32_t attached_ms;
    bool     started;       /* já viu READY (ou esperou o boot do ESP32) */
    bool     info_requested;
    bool     ping_sent;
    bool     require_handshake; /* sem READY/OK no boot = não é o braço (autodetecção) */
    bool     limits_new;
    struct backend_limits limits;
    bool     have_telemetry;
    float    telemetry[JOINT_COUNT];
    bool     synced;        /* já adotou a posição real (1º T após conectar) */
    bool     sync_new;
    uint32_t started_ms;
    char     firmware[48];
};

void link_init(struct text_link *link, const char *tag, bool verbose);
void link_attach(struct text_link *link, int fd, uint32_t now_ms);
void link_detach(struct text_link *link, const char *why);
bool link_send_line(struct text_link *link, const char *line);

/* Lê e interpreta o que chegou. Retorna false se a conexão caiu. */
bool link_poll(struct text_link *link, uint32_t now_ms);

/* true se o dispositivo não respondeu READY/OK dentro do tempo de boot. */
bool link_handshake_failed(const struct text_link *link, uint32_t now_ms);

/* Envia J (após o boot) e cuida do handshake READY/I. */
void link_send_targets(struct text_link *link, const float target[JOINT_COUNT], uint32_t now_ms);

/* ---- fábricas (um arquivo por backend) ---- */

struct backend_ops
{
    void (*update)(void *impl, const float target[JOINT_COUNT], uint32_t now_ms);
    void (*note)(void *impl, const char *text);
    struct text_link *(*link)(void *impl); /* NULL se não usa protocolo de texto */
    const char *(*status)(void *impl);
    void (*close)(void *impl);
};

void *backend_serial_open(const struct backend_options *opt, const struct backend_ops **ops);
void *backend_tcp_open(const struct backend_options *opt, const struct backend_ops **ops);
void *backend_gpio_open(const struct backend_options *opt, const struct backend_ops **ops);
