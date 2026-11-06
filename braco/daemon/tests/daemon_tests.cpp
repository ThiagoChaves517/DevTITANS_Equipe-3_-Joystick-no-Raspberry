// Testes do snes_arm_daemon sem hardware (rodam no host): ./test.sh
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../../../daemon/src/controller_device.h"  // snes_input_daemon (branch driver)
#include "../src/forwarder.h"
#include "../src/snes_register.h"
#include "ArmMapping.h"
#include "FrameProtocol.h"

using namespace snes_arm;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) { ++g_failures; std::printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static uint16_t press(std::initializer_list<int> bits) {
    uint16_t s = 0xFFFF;
    for (int b : bits) s = uint16_t(s & ~(1u << b));
    return s;
}

// ---- quadro v1 ----

static void test_contract_vector() {
    // Vetor do driver/docs/protocol_spec.md §2: CONTROLLER_STATE, seq 0, nada pressionado.
    uint8_t f[Protocol::FRAME_SIZE];
    Protocol::encode(Protocol::CONTROLLER_STATE, 0, 0xFF, 0xFF, f);
    const uint8_t expected[] = {0x53, 0x4E, 0x01, 0x01, 0x00, 0xFF, 0xFF, 0x6F};
    CHECK(std::memcmp(f, expected, sizeof(expected)) == 0);
    const char* check = "123456789";
    CHECK(Protocol::crc8(reinterpret_cast<const uint8_t*>(check), 9) == 0xF4);
}

static std::vector<uint8_t> frame(uint16_t state, uint8_t seq, uint8_t type = Protocol::CONTROLLER_STATE) {
    std::vector<uint8_t> f(Protocol::FRAME_SIZE);
    Protocol::encode(type, seq, uint8_t(state), uint8_t(state >> 8), f.data());
    return f;
}

static std::vector<Protocol::Frame> decode_all(Protocol::FrameDecoder& d, const std::vector<uint8_t>& bytes) {
    std::vector<Protocol::Frame> out;
    Protocol::Frame fr;
    for (uint8_t b : bytes)
        if (d.push(b, fr)) out.push_back(fr);
    return out;
}

static void test_decoder_basics() {
    Protocol::FrameDecoder d;
    std::vector<uint8_t> bytes = {0x00, 0x4E, 0x53};  // lixo antes, inclusive um "S" solto
    std::vector<uint8_t> a = frame(0xFEFF, 7);
    bytes.insert(bytes.end(), a.begin(), a.end());
    auto got = decode_all(d, bytes);
    CHECK(got.size() == 1 && got[0].state() == 0xFEFF && got[0].sequence == 7);

    // CRC errado é rejeitado e o quadro seguinte é aceito.
    std::vector<uint8_t> bad = frame(0xFFFE, 8);
    bad[7] ^= 0xFF;
    std::vector<uint8_t> good = frame(0xFFF7, 9);
    bad.insert(bad.end(), good.begin(), good.end());
    got = decode_all(d, bad);
    CHECK(got.size() == 1 && got[0].state() == 0xFFF7);
    CHECK(d.errors() >= 1);

    // Versão e tipo desconhecidos são rejeitados.
    std::vector<uint8_t> v2 = frame(0xFEFF, 1);
    v2[2] = 0x02;
    v2[7] = Protocol::crc8(v2.data(), 7);
    std::vector<uint8_t> t = frame(0xFEFF, 2, 0x42);
    v2.insert(v2.end(), t.begin(), t.end());
    CHECK(decode_all(d, v2).empty());
}

static void test_decoder_resyncs_after_random_noise() {
    // 300 quadros com lixo aleatório entre eles: todos têm de ser recuperados, em ordem.
    std::srand(1234);
    Protocol::FrameDecoder d;
    std::vector<uint8_t> stream;
    std::vector<uint16_t> sent;
    for (int i = 0; i < 300; ++i) {
        const int noise = std::rand() % 6;
        for (int k = 0; k < noise; ++k) stream.push_back(uint8_t(std::rand() % 2 ? 0x53 : std::rand()));
        const uint16_t st = uint16_t(std::rand() | 0xF000);
        std::vector<uint8_t> f = frame(st, uint8_t(i));
        stream.insert(stream.end(), f.begin(), f.end());
        sent.push_back(st);
    }
    auto got = decode_all(d, stream);
    CHECK(got.size() == sent.size());
    bool same = got.size() == sent.size();
    for (size_t i = 0; same && i < got.size(); ++i) same = got[i].state() == sent[i];
    CHECK(same);
}

// ---- eventos -> 16 bits ----

static void test_register_buttons_and_hat() {
    SnesRegister r;
    CHECK(r.is_neutral());
    CHECK(r.apply(code::EV_KEY, code::BTN_A, 1) && r.value() == press({8}));
    CHECK(!r.apply(code::EV_KEY, code::BTN_A, 2));  // autorepeat: continua pressionado
    CHECK(r.apply(code::EV_ABS, code::ABS_HAT0X, -1) && r.value() == press({8, 6}));
    CHECK(r.apply(code::EV_ABS, code::ABS_HAT0X, 1) && r.value() == press({8, 7}));
    CHECK(r.apply(code::EV_ABS, code::ABS_HAT0Y, -1) && r.value() == press({8, 7, 4}));
    CHECK(r.apply(code::EV_ABS, code::ABS_HAT0Y, 0) && r.apply(code::EV_ABS, code::ABS_HAT0X, 0));
    CHECK(!r.apply(code::EV_KEY, code::BTN_0, 1));  // garra do acelerômetro: fora dos 16 bits
    CHECK(r.apply(code::EV_KEY, code::BTN_A, 0) && r.is_neutral());
}

// Liga o ControllerDevice do Raphael direto no SnesRegister: o que ele publica tem de voltar igual.
struct LoopbackSink : snes::InputSink {
    SnesRegister reg;
    bool emit(int t, int c, int v) override { reg.apply(t, c, v); return true; }
    bool sync() override { return true; }
};

static void test_inverse_of_snes_input_daemon() {
    LoopbackSink sink;
    snes::ControllerDevice dev(sink);
    int mismatches = 0;
    for (uint32_t pressed = 0; pressed < 4096; ++pressed) {
        // Combinação em ordem "embaralhada" para exercitar transições variadas.
        const uint32_t combo = (pressed * 2654435761u) & 0xFFF;
        uint16_t state = uint16_t(0xFFFF & ~combo);
        dev.publish(state, 0);
        // O D-pad vira ABS_HAT: os dois sentidos juntos não existem lá (viram 0).
        uint16_t expected = uint16_t(state | 0xF000);
        if ((expected & 0x0030) == 0) expected |= 0x0030;  // UP + DOWN
        if ((expected & 0x00C0) == 0) expected |= 0x00C0;  // LEFT + RIGHT
        if (sink.reg.value() != expected) ++mismatches;
    }
    CHECK(mismatches == 0);
    dev.neutralize();
    CHECK(sink.reg.is_neutral());
}

// ---- 16 bits -> direções (tabela do William) ----

static void test_arm_mapping() {
    using namespace ArmMapping;
    auto d = fromState(press({LEFT}));
    CHECK(d.base == -1 && d.ombro == 0 && d.cotovelo == 0 && d.garra == 0);
    d = fromState(press({RIGHT, UP, B, A}));
    CHECK(d.base == 1 && d.ombro == 1 && d.cotovelo == 1 && d.garra == 1);
    d = fromState(press({DOWN, Y, X}));
    CHECK(d.ombro == -1 && d.cotovelo == -1 && d.garra == -1);
    d = fromState(press({A, X}));  // abrir e fechar juntos: não mexe
    CHECK(d.garra == 0);
    d = fromState(press({START, B, LEFT}));  // Start para os movimentos
    CHECK(d.base == 0 && d.cotovelo == 0);
    d = fromState(press({SELECT, L, R}));  // não mexem no braço
    CHECK(d.base == 0 && d.ombro == 0 && d.cotovelo == 0 && d.garra == 0);
}

// ---- política de envio ----

struct FakeLink : FrameSink {
    bool ready = false;
    bool fail = false;
    std::vector<std::vector<uint8_t>> frames;
    bool is_ready() const override { return ready; }
    bool send(const uint8_t* d, size_t n) override {
        if (fail) return false;
        frames.emplace_back(d, d + n);
        return true;
    }
    uint16_t last_state() const { return uint16_t(frames.back()[5] | (frames.back()[6] << 8)); }
};

static void test_forwarder_policy() {
    FakeLink link;
    Forwarder f(link, 100);

    f.tick(0);
    CHECK(link.frames.empty());  // link ainda não pronto (boot do ESP32)
    CHECK(f.next_wakeup_ms(0) == -1);  // pendente, mas sem link: não pode girar o laço em vazio

    link.ready = true;
    f.tick(10);
    CHECK(link.frames.size() == 1 && link.last_state() == 0xFFFF);  // sincroniza ao ficar pronto

    f.tick(500);
    CHECK(link.frames.size() == 1);  // nada pressionado: sem tráfego
    CHECK(f.next_wakeup_ms(500) == -1);

    f.publish(press({8}));  // A
    f.tick(510);
    CHECK(link.frames.size() == 2 && link.last_state() == press({8}));
    CHECK(link.frames[1][4] == 1);  // seq incrementa

    f.publish(press({8}));  // repetido
    f.tick(560);
    CHECK(link.frames.size() == 2);
    CHECK(f.next_wakeup_ms(560) == 50);

    f.tick(610);  // keepalive de 100 ms enquanto pressionado
    CHECK(link.frames.size() == 3 && link.last_state() == press({8}));

    f.publish(0xFFFF);  // soltou: vai uma vez e para
    f.tick(620);
    f.tick(900);
    CHECK(link.frames.size() == 4 && link.last_state() == 0xFFFF);

    f.publish(0x0FFF);  // bits 12-15 sempre em 1: continua neutro
    f.tick(910);
    CHECK(link.frames.size() == 4);

    link.ready = false;  // cabo solto
    f.publish(press({0}));
    f.tick(950);
    CHECK(link.frames.size() == 4);
    link.ready = true;  // voltou (ESP32 reiniciou)
    f.link_restarted();
    f.tick(1500);
    CHECK(link.frames.size() == 5 && link.last_state() == press({0}));

    link.fail = true;  // falha de escrita: tenta de novo depois
    f.publish(press({0, 1}));
    f.tick(1510);
    CHECK(f.send_errors() == 1 && link.frames.size() == 5);
    link.fail = false;
    f.tick(1520);
    CHECK(link.frames.size() == 6 && link.last_state() == press({0, 1}));
    CHECK(f.frames_sent() == 6);
}

int main() {
    test_contract_vector();
    test_decoder_basics();
    test_decoder_resyncs_after_random_noise();
    test_register_buttons_and_hat();
    test_inverse_of_snes_input_daemon();
    test_arm_mapping();
    test_forwarder_policy();
    std::printf("%d verificações, %d falhas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
