// Política de envio dos 16 bits ao braço. Contrato: braco/docs/protocolo_serial.md.
//
// Mesma política do app do Raphael para o daemon (driver/docs/ipc_spec.md §3):
//   - estado completo em cada quadro;
//   - envia na mudança e, enquanto houver algo pressionado, reenvia a cada keepalive_ms
//     (o ESP32 zera as direções após 250 ms sem quadro);
//   - nada pressionado: sem tráfego;
//   - link novo (ESP32 reiniciado/reconectado): reenvia o estado atual.
//
// Wire::William (firmware do William, branch FirmwareBraco, FirmawareBraco/firmwareBraco.ino):
//   - cada mensagem é uma linha de texto com o número decimal dos botões pressionados, bit 1 =
//     pressionado (o inverso do registrador): "256\n" = A. Lida com Serial.parseInt();
//   - o firmware lê no máximo UMA linha a cada 25 ms e anda 1° por linha recebida. Então: enquanto
//     houver botão pressionado, envia uma linha a cada keepalive_ms (30 ms), NUNCA mais rápido do que
//     isso (nem na mudança), para não acumular linhas na serial e o braço continuar andando depois de
//     soltar; nada pressionado: não envia nada (o braço para porque as linhas param).
#pragma once
#include <cstddef>
#include <cstdint>

namespace snes_arm {

// Formato no fio. Frame: quadro binário de 8 bytes (SNES_ARM_ESP32, simulador). William: texto decimal.
enum class Wire { Frame, William };

// Linha do Wire::William para um estado (ativo em LOW): "<botões pressionados em decimal>\n".
// Retorna o tamanho escrito em out (precisa de pelo menos 8 bytes).
size_t encode_william(uint16_t state, char* out, size_t len);

// Destino dos quadros (serial em produção, fake nos testes).
class FrameSink {
public:
    virtual ~FrameSink() = default;
    virtual bool is_ready() const = 0;
    virtual bool send(const uint8_t* data, size_t len) = 0;
};

class Forwarder {
public:
    Forwarder(FrameSink& sink, long keepalive_ms, Wire wire = Wire::Frame)
        : sink_(sink), keepalive_ms_(keepalive_ms), wire_(wire) {}

    // Novo estado do controle (registrador SNES, ativo em LOW).
    void publish(uint16_t state);

    // Chamado a cada volta do laço: envia o que estiver pendente e o keepalive.
    void tick(long long now_ms);

    // O link caiu e voltou: o ESP32 pode ter reiniciado, então o estado atual vai de novo.
    void link_restarted() { pending_ = true; }

    // Quanto o laço pode dormir até o próximo envio (-1 = só quando algo mudar ou o link voltar).
    long next_wakeup_ms(long long now_ms) const;

    uint16_t state() const { return state_; }
    bool is_neutral() const { return state_ == 0xFFFF; }
    unsigned long frames_sent() const { return frames_sent_; }
    unsigned long send_errors() const { return send_errors_; }

private:
    bool send_now(long long now_ms);

    FrameSink& sink_;
    long keepalive_ms_;
    Wire wire_;
    uint16_t state_ = 0xFFFF;
    bool pending_ = true;  // começa sincronizando: o ESP32 recebe o estado logo que o link fica pronto
    long long last_send_ms_ = -1000000;  // "nunca": o primeiro envio não espera o espaçamento
    uint8_t seq_ = 0;
    unsigned long frames_sent_ = 0;
    unsigned long send_errors_ = 0;
};

}  // namespace snes_arm
