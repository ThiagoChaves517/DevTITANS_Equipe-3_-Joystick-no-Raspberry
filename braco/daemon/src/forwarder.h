// Política de envio dos 16 bits ao braço. Contrato: braco/docs/protocolo_serial.md.
//
// Mesma política do app do Raphael para o daemon (driver/docs/ipc_spec.md §3):
//   - estado completo em cada quadro;
//   - envia na mudança e, enquanto houver algo pressionado, reenvia a cada keepalive_ms
//     (o ESP32 zera as direções após 250 ms sem quadro);
//   - nada pressionado: sem tráfego;
//   - link novo (ESP32 reiniciado/reconectado): reenvia o estado atual.
#pragma once
#include <cstddef>
#include <cstdint>

namespace snes_arm {

// Destino dos quadros (serial em produção, fake nos testes).
class FrameSink {
public:
    virtual ~FrameSink() = default;
    virtual bool is_ready() const = 0;
    virtual bool send(const uint8_t* data, size_t len) = 0;
};

class Forwarder {
public:
    Forwarder(FrameSink& sink, long keepalive_ms) : sink_(sink), keepalive_ms_(keepalive_ms) {}

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
    uint16_t state_ = 0xFFFF;
    bool pending_ = true;  // começa sincronizando: o ESP32 recebe o estado logo que o link fica pronto
    long long last_send_ms_ = 0;
    uint8_t seq_ = 0;
    unsigned long frames_sent_ = 0;
    unsigned long send_errors_ = 0;
};

}  // namespace snes_arm
