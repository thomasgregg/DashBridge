#include "call_protocol.hpp"
#include "call_resampler.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <random>
using namespace calls;
static std::vector<uint8_t> tone(unsigned rate, double frequency) {
    std::vector<uint8_t> result(rate / 5 * 2); // 200 ms; several callback boundaries.
    for (size_t i = 0; i < result.size() / 2; ++i)
        pcm_write(result.data() + 2 * i, int16_t(12000 * std::sin(2 * 3.141592653589793 * frequency * i / rate)));
    return result;
}
static double rms(const std::vector<uint8_t> &pcm) {
    double total = 0;
    for (size_t i = 200; i < pcm.size(); i += 2) {
        double value = pcm_read(pcm.data() + i); total += value * value;
    }
    return std::sqrt(total / ((pcm.size() - 200) / 2));
}
static double component(const std::vector<uint8_t> &pcm, double frequency) {
    double re = 0, im = 0;
    for (size_t i = 200; i < pcm.size(); i += 2) {
        double phase = 2 * 3.141592653589793 * frequency * (i / 2) / 16000;
        re += pcm_read(pcm.data() + i) * std::cos(phase);
        im += pcm_read(pcm.data() + i) * std::sin(phase);
    }
    return std::sqrt(re * re + im * im);
}
static void resampler_tests() {
    auto input = tone(8000, 1000);
    std::vector<uint8_t> wide(input.size() * 2), chunks(wide.size()), restored(input.size());
    PcmUpsampler up, split_up;
    up.convert(input.data(), input.size(), wide.data());
    for (size_t i = 0; i < input.size(); i += 2)
        split_up.convert(input.data() + i, 2, chunks.data() + 2 * i);
    assert(chunks == wide); // Callback fragmentation must not alter signal or timing.
    assert(rms(wide) / rms(input) > .97 && rms(wide) / rms(input) < 1.03);
    assert(component(wide, 7000) / component(wide, 1000) < .01); // Suppress interpolation image.
    PcmDownsampler down, split_down;
    down.convert(wide.data(), wide.size(), restored.data());
    chunks.resize(restored.size());
    for (size_t i = 0; i < wide.size(); i += 4)
        split_down.convert(wide.data() + i, 4, chunks.data() + i / 2);
    assert(chunks == restored);
    assert(rms(restored) / rms(input) > .94 && rms(restored) / rms(input) < 1.06);
    auto high = tone(16000, 6000);
    down.clear(); down.convert(high.data(), high.size(), restored.data());
    assert(rms(restored) / rms(high) < .01); // >40 dB anti-alias rejection before 8 kHz fallback.
    auto speech = tone(16000, 3000);
    down.clear(); down.convert(speech.data(), speech.size(), restored.data());
    assert(rms(restored) / rms(speech) > .95);
    // Disconnect/codec-change reset must remove previous speech from filter history.
    std::vector<uint8_t> silence(input.size());
    up.clear(); up.convert(silence.data(), silence.size(), wide.data());
    assert(rms(wide) == 0);
    down.clear(); down.convert(wide.data(), wide.size(), restored.data());
    assert(rms(restored) == 0);
    // Full-scale inputs must saturate rather than wrap, including filter overshoot.
    for (size_t i = 0; i < input.size(); i += 2) pcm_write(input.data() + i, 32767);
    up.clear(); up.convert(input.data(), input.size(), wide.data());
    for (size_t i = 100; i < wide.size(); i += 2) assert(pcm_read(wide.data() + i) > 32000);
    for (size_t i = 0; i < input.size(); i += 2) pcm_write(input.data() + i, -32768);
    up.clear(); up.convert(input.data(), input.size(), wide.data());
    for (size_t i = 100; i < wide.size(); i += 2) assert(pcm_read(wide.data() + i) < -32000);
}
int main() {
    resampler_tests();
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
    assert(decoder.crc_failures() == 1);
    std::array<uint8_t, 136> legacy{};
    legacy[0] = 'D'; legacy[1] = 'A'; legacy[2] = 1; legacy[3] = 120;
    auto legacy_crc = crc16(legacy.data(), legacy.size() - 2);
    legacy[134] = legacy_crc; legacy[135] = legacy_crc >> 8;
    for (auto c : legacy) decoder.feed(c, accept);
    assert(received == 1); // Never play v1 8 kHz data at 16 kHz.
    for (int i = 0; i < 55; ++i) decoder.feed(frame[i], accept); // Truncated frame followed by fresh frame.
    for (auto c : frame) decoder.feed(c, accept);
    assert(received == 2);
    std::mt19937 random(42);
    for (int i = 0; i < 100000; ++i) decoder.feed(uint8_t(random()), [](const Frame &) {});
    AudioSequence order;
    assert(order.accept(65535, 1000) == 2);
    assert(order.accept(0, 8500) == 1); // Normal 16-bit sequence wrap.
    assert(order.accept(0, 16000) == 0); // Duplicate.
    assert(order.accept(65535, 16000) == 0); // Older packet.
    assert(order.accept(2, 23500) == 2); // Missing packet clears queued PCM.
    assert(order.accept(3, 100000) == 2); // Re-prime after a long audio pause.
    assert(order.accept(40000, 300000000) == 2); // Recover after a minutes-long wire interruption.
    assert(order.accept(40000, 300080000) == 0); // Even after silence, don't replay the same packet.
    assert(order.accept(40001, 300090000) == 2);
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
    std::cout << "Call control, 16 kHz PCM transport and filtered 8/16 kHz conversion tests passed\n";
}
