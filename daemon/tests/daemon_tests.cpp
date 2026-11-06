// Testes do daemon sem hardware (rodam no host): ./test.sh
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../src/client_session.h"
#include "../src/controller_device.h"
#include "../src/ipc_protocol.h"

using namespace snes;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) { ++g_failures; std::printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

struct Event { int type, code, value; };

struct FakeSink : InputSink {
    std::vector<Event> events;
    int syncs = 0;
    bool emit(int t, int c, int v) override { events.push_back({t, c, v}); return true; }
    bool sync() override { ++syncs; return true; }
    bool has(int t, int c, int v) const {
        for (const Event& e : events) if (e.type == t && e.code == c && e.value == v) return true;
        return false;
    }
    void clear() { events.clear(); syncs = 0; }
};

static uint16_t press(std::initializer_list<int> bits) {
    uint16_t s = 0xFFFF;
    for (int b : bits) s = uint16_t(s & ~(1u << b));
    return s;
}

static std::vector<uint8_t> msg(uint8_t type, uint16_t seq, uint16_t buttons, uint8_t grip) {
    ipc::Message m;
    m.type = type; m.seq = seq; m.buttons = buttons; m.grip = grip;
    std::vector<uint8_t> out(ipc::kMessageSize);
    ipc::encode(m, out.data());
    return out;
}

static void test_contract_vector_matches_java() {
    // Mesmo vetor de IpcProtocolTest.stateMessageLayout.
    std::vector<uint8_t> m = msg(ipc::STATE, 0x1234, 0xFEFE, 1);
    const uint8_t expected[] = {0x53, 0x44, 0x01, 0x02, 0x34, 0x12, 0xFE, 0xFE, 0x01, 0x00, 0x00, 0x81};
    CHECK(std::memcmp(m.data(), expected, sizeof(expected)) == 0);
    const char* check = "123456789";
    CHECK(ipc::crc8(reinterpret_cast<const uint8_t*>(check), 9) == 0xF4);
}

static void test_decode_errors() {
    ipc::Message out;
    std::vector<uint8_t> m = msg(ipc::STATE, 1, 0xFFFF, 0);
    CHECK(ipc::decode(m.data(), out) == ipc::DecodeError::NONE);

    auto mutate = [](std::vector<uint8_t> v, size_t i, uint8_t value, bool fix_crc) {
        v[i] = value;
        if (fix_crc) v[11] = ipc::crc8(v.data(), 11);
        return v;
    };
    CHECK(ipc::decode(mutate(m, 0, 0x00, true).data(), out) == ipc::DecodeError::INVALID_MAGIC);
    CHECK(ipc::decode(mutate(m, 2, 0x02, true).data(), out) == ipc::DecodeError::UNSUPPORTED_VERSION);
    CHECK(ipc::decode(mutate(m, 3, 0x81, true).data(), out) == ipc::DecodeError::UNKNOWN_TYPE);  // ACK não vem do cliente
    CHECK(ipc::decode(mutate(m, 6, 0x00, false).data(), out) == ipc::DecodeError::CRC_MISMATCH);
    CHECK(ipc::decode(mutate(m, 8, 3, true).data(), out) == ipc::DecodeError::INVALID_FIELD);    // grip fora do domínio
    CHECK(ipc::decode(mutate(m, 10, 1, true).data(), out) == ipc::DecodeError::INVALID_FIELD);   // reservado != 0
    CHECK(ipc::decode(msg(ipc::NEUTRAL, 1, 0xFFFE, 0).data(), out) == ipc::DecodeError::INVALID_FIELD);
}

static void test_button_mapping_and_transitions() {
    FakeSink sink;
    ControllerDevice dev(sink);
    CHECK(!dev.publish(0xFFFF, 0));  // já neutro: nenhum evento
    CHECK(sink.events.empty() && sink.syncs == 0);

    CHECK(dev.publish(press({0, 8}), 0));  // B + A
    CHECK(sink.has(code::EV_KEY, code::BTN_B, 1));
    CHECK(sink.has(code::EV_KEY, code::BTN_A, 1));
    CHECK(sink.events.size() == 2 && sink.syncs == 1);

    sink.clear();
    CHECK(!dev.publish(press({0, 8}), 0));  // repetido: sem eventos
    CHECK(sink.events.empty());

    sink.clear();
    CHECK(dev.publish(press({8}), 0));  // solta B
    CHECK(sink.events.size() == 1 && sink.has(code::EV_KEY, code::BTN_B, 0));

    const struct { int bit, code; } all[] = {{0, code::BTN_B}, {1, code::BTN_Y}, {2, code::BTN_SELECT}, {3, code::BTN_START},
                                             {8, code::BTN_A}, {9, code::BTN_X}, {10, code::BTN_TL}, {11, code::BTN_TR}};
    for (auto& a : all) {
        FakeSink s;
        ControllerDevice d(s);
        d.publish(press({a.bit}), 0);
        CHECK(s.events.size() == 1 && s.has(code::EV_KEY, a.code, 1));
    }
}

static void test_dpad_hat() {
    FakeSink sink;
    ControllerDevice dev(sink);
    dev.publish(press({4}), 0);  // UP
    CHECK(sink.has(code::EV_ABS, code::ABS_HAT0Y, -1));
    sink.clear();
    dev.publish(press({4, 7}), 0);  // UP + RIGHT
    CHECK(sink.events.size() == 1 && sink.has(code::EV_ABS, code::ABS_HAT0X, 1));
    sink.clear();
    dev.publish(press({6, 7}), 0);  // LEFT + RIGHT se anulam; UP solto
    CHECK(sink.has(code::EV_ABS, code::ABS_HAT0X, 0) && sink.has(code::EV_ABS, code::ABS_HAT0Y, 0));
    CHECK(ControllerDevice::hat_y(press({5})) == 1);
}

static void test_grip_and_unused_bits() {
    FakeSink sink;
    ControllerDevice dev(sink);
    CHECK(!dev.publish(0x0FFF, 0));  // bits 12-15 não são botões
    dev.publish(0xFFFF, 1);
    CHECK(sink.has(code::EV_KEY, code::BTN_0, 1));
    sink.clear();
    dev.publish(0xFFFF, 2);
    CHECK(sink.has(code::EV_KEY, code::BTN_0, 0) && sink.has(code::EV_KEY, code::BTN_1, 1));
}

static void test_neutralize_once() {
    FakeSink sink;
    ControllerDevice dev(sink);
    dev.publish(press({0, 3, 4}), 2);
    sink.clear();
    CHECK(dev.neutralize());
    CHECK(sink.has(code::EV_KEY, code::BTN_B, 0) && sink.has(code::EV_KEY, code::BTN_START, 0));
    CHECK(sink.has(code::EV_ABS, code::ABS_HAT0Y, 0) && sink.has(code::EV_KEY, code::BTN_1, 0));
    CHECK(dev.is_neutral());
    sink.clear();
    CHECK(!dev.neutralize());
    CHECK(sink.events.empty());
}

static void test_session_handshake_and_reframing() {
    FakeSink sink;
    ControllerDevice dev(sink);
    ClientSession s(dev);
    std::vector<uint8_t> reply;
    std::string reason;

    std::vector<uint8_t> hello = msg(ipc::HELLO, 7, 0xFFFF, 0);
    CHECK(s.consume(hello.data(), 5, reply, reason));  // mensagem parcial
    CHECK(reply.empty() && s.partial_bytes() == 5);
    CHECK(s.consume(hello.data() + 5, 7, reply, reason));
    CHECK(reply.size() == ipc::kMessageSize && reply[3] == ipc::HELLO_ACK && reply[9] == ipc::STATUS_OK);
    CHECK(reply[11] == ipc::crc8(reply.data(), 11));

    // Duas mensagens num único read.
    std::vector<uint8_t> two = msg(ipc::STATE, 8, press({0}), 0);
    std::vector<uint8_t> b = msg(ipc::STATE, 9, press({0, 1}), 0);
    two.insert(two.end(), b.begin(), b.end());
    reply.clear();
    CHECK(s.consume(two.data(), two.size(), reply, reason));
    CHECK(dev.buttons() == (press({0, 1}) | 0xF000) && s.messages() == 3 && s.last_seq() == 9);

    std::vector<uint8_t> n = msg(ipc::NEUTRAL, 10, 0xFFFF, 0);
    CHECK(s.consume(n.data(), n.size(), reply, reason));
    CHECK(dev.is_neutral());
}

static void test_session_rejects() {
    FakeSink sink;
    ControllerDevice dev(sink);
    std::vector<uint8_t> reply;
    std::string reason;
    {
        ClientSession s(dev);
        std::vector<uint8_t> st = msg(ipc::STATE, 1, press({0}), 0);
        CHECK(!s.consume(st.data(), st.size(), reply, reason));
        CHECK(reason.find("HELLO") != std::string::npos);
        CHECK(dev.is_neutral());
    }
    {
        ClientSession s(dev);
        std::vector<uint8_t> h = msg(ipc::HELLO, 1, 0xFFFF, 0);
        s.consume(h.data(), h.size(), reply, reason);
        std::vector<uint8_t> bad = msg(ipc::STATE, 2, press({0}), 0);
        bad[11] ^= 0xFF;
        CHECK(!s.consume(bad.data(), bad.size(), reply, reason));
        CHECK(reason.find("CRC_MISMATCH") != std::string::npos);
        CHECK(dev.is_neutral());  // mensagem inválida não altera estado
    }
}

int main() {
    test_contract_vector_matches_java();
    test_decode_errors();
    test_button_mapping_and_transitions();
    test_dpad_hat();
    test_grip_and_unused_bits();
    test_neutralize_once();
    test_session_handshake_and_reframing();
    test_session_rejects();
    std::printf("%d verificações, %d falhas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
