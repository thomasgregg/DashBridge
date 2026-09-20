#include "call_protocol.hpp"
#include <cassert>
#include <iostream>
#include <random>
using namespace calls;
int main() {
    State a{1, 0xdeadbeef, 0x12345678, 0, 1, 0, 1, 4, 0, 5, 1, "+491234"}, b;
    assert(decode_state(encode_state(a), a.number, b) && b.audio == a.audio && b.generation == a.generation);
    for (const auto &bad : {"", "1 2", "1 0 0 0 4 0 1 4 0 5 1", "1 4294967296 0 0 0 0 0 0 0 0 0",
                           "1 0 0 0 0 0 0 0 0 0 0 junk", "1 0 0 0 0 0 0 0 0 0 -1"})
        assert(!decode_state(bad, "", b));
    assert(!decode_state(encode_state(a), "x\r\nATD123;", b));
    assert(command_allowed(a, "answer", ""));
    assert(!command_allowed(a, "dial", "123"));
    assert(!command_allowed(a, "answer", "extra"));
    assert(!command_allowed(a, "dtmf", "1"));
    a.call = 1; a.setup = 0;
    assert(command_allowed(a, "hangup", ""));
    assert(command_allowed(a, "dtmf", "#"));
    assert(!command_allowed(a, "dtmf", "12"));
    a.held = 1; assert(!command_allowed(a, "hangup", ""));
    a.held = 0; a.linked = 0; assert(!command_allowed(a, "hangup", ""));
    State idle; idle.linked = 1;
    assert(command_allowed(idle, "dial", "+491234"));
    for (const auto &invalid : {"", "+", "+49+12", "123;ATD456", "123\r\n", "123;"})
        assert(!command_allowed(idle, "dial", invalid));
    assert(!command_allowed(idle, "redial", ""));
    idle.setup = 1; assert(!command_allowed(idle, "dial", "123"));
    CommandGate gate; gate.reset(10);
    assert(!gate.accept(9, 1)); assert(!gate.accept(10, 0)); assert(gate.accept(10, 5));
    assert(!gate.accept(10, 5)); assert(!gate.accept(10, 4)); assert(gate.accept(10, 6));
    gate.reset(11); assert(!gate.accept(10, 7)); assert(gate.accept(11, 1));
    std::array<uint8_t, pcm_size> pcm{};
    for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = uint8_t(i);
    auto frame = audio_frame(0x1234, 0xabcd, 65535, pcm.data());
    assert(get32(frame.data() + 4) == 0x1234 && get32(frame.data() + 8) == 0xabcd);
    AudioDecoder decoder;
    int received = 0;
    auto accept = [&](const Frame &f) { assert(f == frame); ++received; };
    for (auto c : frame) decoder.feed(c, accept);
    assert(received == 1);
    auto corrupt = frame; corrupt[18] ^= 1;
    for (auto c : corrupt) decoder.feed(c, accept);
    assert(received == 1);
    for (int i = 0; i < 55; ++i) decoder.feed(frame[i], accept); // Truncated frame followed by fresh frame.
    for (auto c : frame) decoder.feed(c, accept);
    assert(received == 2);
    std::mt19937 random(42);
    for (int i = 0; i < 100000; ++i) decoder.feed(uint8_t(random()), [](const Frame &) {});
    PcmBuffer buffer;
    std::array<uint8_t, pcm_size * 8> output{};
    assert(buffer.pop(output.data(), pcm_size) == 0);
    assert(buffer.push(pcm.data(), pcm_size));
    assert(buffer.pop(output.data(), pcm_size) == 0); // Wait for jitter prefill.
    assert(buffer.push(pcm.data(), pcm_size));
    assert(buffer.pop(output.data(), pcm_size) == pcm_size);
    assert(memcmp(output.data(), pcm.data(), pcm_size) == 0);
    buffer.clear(); assert(buffer.pop(output.data(), pcm_size) == 0);
    for (int i = 0; i < 8; ++i) assert(buffer.push(pcm.data(), pcm_size));
    assert(!buffer.push(pcm.data(), pcm_size)); // Overflow discards old backlog.
    assert(buffer.pop(output.data(), pcm_size) == 0);
    assert(!buffer.push(pcm.data(), 1));
    // The control channel must preserve the new operation and all state fields.
    bridge::WireMessage m{bridge::Op::call, 33, {}};
    m.notice.app = protocol; m.notice.title = "state"; m.notice.body = encode_state(a);
    auto wire = bridge::encode(m); bridge::WireDecoder d;
    bool delivered = false;
    for (auto c : wire) d.feed(&c, 1, [&](const bridge::WireMessage &out) {
        assert(out.op == bridge::Op::call && out.session == 33 && out.notice.body == m.notice.body); delivered = true;
    });
    assert(delivered);
    std::cout << "Call control and PCM transport tests passed\n";
}
