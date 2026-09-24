#include "dashbridge/protocols/calls_v3.hpp"
#include "dashbridge/adapters/call_resampler.hpp"
#include "dashbridge/protocols/music_v1.hpp"
#include "dashbridge/protocols/dashlink_v2.hpp"
#include "dashbridge/protocols/contacts_v1.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <random>
using namespace calls;
namespace contacts = dashbridge::protocols::contacts_v1;
namespace dashlink = dashbridge::protocols::dashlink_v2;
namespace music_core = dashbridge::core::music;
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
static EncodedAudio encoded_sample(uint8_t marker) {
    EncodedAudio audio;
    audio.codec = AudioCodec::msbc; audio.size = msbc_frame_size;
    audio.data.fill(marker); return audio;
}
static void contact_parser_tests() {
    const std::string cards = "BEGIN:VCARD\r\nVERSION:3.0\r\nN:Doe;Jane;;;\r\nFN:Jane Doe\r\n"
        "TEL;TYPE=CELL:+49 (123) 45-67\r\nTEL;TYPE=WORK:555-0100\r\n"
        "ADR;TYPE=HOME:;;Main Street 1;Berlin;;10115;Germany\r\nEND:VCARD\r\n"
        "BEGIN:VCARD\nN:Smith;John;;;\nTEL:12345\nX-IRMC-CALL-DATETIME:20260922T101500\nEND:VCARD\n";
    contacts::Parser parser;
    std::vector<contacts::Contact> parsed;
    for (size_t i = 0; i < cards.size(); i += 7) {
        const size_t n = std::min<size_t>(7, cards.size() - i);
        parser.feed(reinterpret_cast<const uint8_t *>(cards.data() + i), n,
                    [&](const contacts::Contact &contact) { parsed.push_back(contact); });
    }
    assert(parsed.size() == 2);
    assert(parsed[0].name == "Jane Doe" && parsed[0].numbers.size() == 2);
    assert(parsed[0].numbers[0].type == "CELL" && parsed[0].numbers[0].value == "+491234567");
    assert(parsed[0].numbers[1].type == "WORK" && parsed[0].numbers[1].value == "5550100");
    assert(parsed[0].addresses.size() == 1 && parsed[0].addresses[0] == "Main Street 1, Berlin, 10115, Germany");
    assert(parsed[1].name == "John Smith" && parsed[1].numbers[0].value == "12345");
    assert(parsed[1].timestamp == "20260922T101500");
    std::string huge = "BEGIN:VCARD\nFN:" + std::string(600, 'x') + "\nTEL:1\nEND:VCARD\n";
    parser.feed(reinterpret_cast<const uint8_t *>(huge.data()), huge.size(), [](const contacts::Contact &) {});
    assert(parser.overflow() == 1);
}
static void music_transport_tests() {
    music::Audio audio;
    audio.stream = 0x10203040;
    audio.sequence = 65535;
    audio.frames = 5;
    audio.timestamp = 0xaabbccdd;
    audio.size = 733;
    for (unsigned i = 0; i < audio.size; ++i)
        audio.data[i] = uint8_t(i * 17);
    music::Frame frame;
    const size_t size = music::encode(audio, frame);
    assert(size == music::header_size + audio.size + 2);

    music::Decoder decoder;
    unsigned received = 0;
    auto accept = [&](const music::Audio &out) {
        assert(out.stream == audio.stream && out.sequence == audio.sequence);
        assert(out.frames == audio.frames && out.timestamp == audio.timestamp && out.size == audio.size);
        assert(!memcmp(out.data.data(), audio.data.data(), audio.size));
        ++received;
    };
    for (size_t i = 0; i < size; ++i)
        decoder.feed(frame[i], accept);
    assert(received == 1);

    auto corrupt = frame;
    corrupt[100] ^= 1;
    for (size_t i = 0; i < size; ++i)
        decoder.feed(corrupt[i], accept);
    assert(received == 1 && decoder.crc_failures() == 1);

    // A truncated packet followed by a complete one must resynchronize without
    // ever emitting partial or corrupted music.
    for (size_t i = 0; i < 100; ++i)
        decoder.feed(frame[i], accept);
    for (size_t i = 0; i < size; ++i)
        decoder.feed(frame[i], accept);
    assert(received == 2);

    music::Audio invalid;
    assert(music::encode(invalid, frame) == 0);
    invalid.stream = 1;
    invalid.frames = 1;
    invalid.size = music::max_payload + 1;
    assert(music::encode(invalid, frame) == 0);
}
static void music_control_tests() {
    music_core::State state;
    state.session = 0x10203040;
    state.revision = 17;
    state.playback = 1;
    state.length_ms = 234567;
    state.position_ms = 45678;
    state.title = "A title";
    state.artist = "An artist";
    state.album = "An album";
    state.track = "3";
    state.track_count = "12";
    state.genre = "Electronic";
    const dashlink::Packet message = dashlink::MusicState{state};
    const auto &decoded = std::get<dashlink::MusicState>(message).state;
    assert(decoded.session == state.session && decoded.revision == state.revision);
    assert(decoded.playback == state.playback && decoded.length_ms == state.length_ms);
    assert(decoded.position_ms == state.position_ms && decoded.title == state.title);
    assert(decoded.artist == state.artist && decoded.album == state.album);
    assert(decoded.track == state.track && decoded.track_count == state.track_count && decoded.genre == state.genre);

    auto wire = dashlink::encode(message);
    dashlink::Decoder decoder;
    bool delivered = false;
    decoder.feed(wire.data(), wire.size(), [&](dashlink::Packet out) {
        const auto &roundtrip = std::get<dashlink::MusicState>(out).state;
        assert(roundtrip.title == state.title && roundtrip.position_ms == state.position_ms);
        delivered = true;
    });
    assert(delivered);

    const dashlink::Packet command = dashlink::MusicCommand{99, 5, 0x4b, 1};
    const auto control = std::get<dashlink::MusicCommand>(command);
    assert(control.key == 0x4b && control.state == 1);
    assert(dashlink::encode(dashlink::MusicCommand{99, 5, 0x4b, 2}).empty());
}
static void encoded_playout_tests() {
    const auto silence = msbc_silence_audio();
    assert(silence.codec == AudioCodec::msbc && silence.size == msbc_frame_size);
    assert(!memcmp(silence.data.data(), msbc_silence_frame.data(), msbc_frame_size));
    assert(silence.data[0] == 0xad); // mSBC syncword; real decoder coverage is in test_audio_codec.py.

    EncodedJitterBuffer jitter;
    EncodedAudio output;
    size_t trimmed = 0;
    const auto speech = encoded_sample(0x42);
    for (int i = 0; i < 5; ++i) assert(jitter.push(speech));
    assert(jitter.pop(AudioCodec::msbc, output, trimmed) == EncodedPlayout::wait && trimmed == 0);
    assert(jitter.push(speech));
    assert(jitter.pop(AudioCodec::msbc, output, trimmed) == EncodedPlayout::audio);
    assert(output.data == speech.data && jitter.size() == 5);
    assert(jitter.pop(AudioCodec::msbc, output, trimmed) == EncodedPlayout::audio && jitter.size() == 4);
    assert(jitter.pop(AudioCodec::msbc, output, trimmed) == EncodedPlayout::conceal);
    assert(jitter.size() == 4); // Concealment must not consume queued speech.
    assert(jitter.push(speech));
    assert(jitter.pop(AudioCodec::msbc, output, trimmed) == EncodedPlayout::audio && jitter.size() == 4);

    jitter.clear();
    for (int i = 0; i < 20; ++i) assert(jitter.push(encoded_sample(uint8_t(i))));
    assert(jitter.pop(AudioCodec::msbc, output, trimmed) == EncodedPlayout::audio);
    assert(trimmed == 8 && jitter.size() == 11 && output.data[0] == 8); // Bound latency to 90 ms before playout.
    for (int i = 20; i < 32; ++i) assert(jitter.push(encoded_sample(uint8_t(i))));
    for (int i = 32; i < 44; ++i) jitter.push(encoded_sample(uint8_t(i)));
    assert(jitter.size() == 32); // Overflow always drops oldest, never grows latency.

    EncodedAudio wrong = speech; wrong.codec = AudioCodec::cvsd;
    jitter.clear(); for (int i = 0; i < 6; ++i) assert(jitter.push(wrong));
    assert(jitter.pop(AudioCodec::msbc, output, trimmed) == EncodedPlayout::codec_mismatch);
    assert(jitter.size() == 0);

    // Ten simulated minutes per clock direction. Reproduce the measured 1.8%
    // radio impairment, two-tick scheduler stalls and +/-400 ppm SCO clock drift.
    for (int drift_ppm : {-400, 400}) {
        jitter.clear();
        double source_phase = 0;
        unsigned held = 0, startup_wait = 0, concealed = 0, trims = 0;
        unsigned consecutive_conceal = 0, longest_conceal = 0, radio_silence = 0;
        for (unsigned tick = 0; tick < 80000; ++tick) { // 600 seconds at 7.5 ms/frame.
            source_phase += 1.0 + drift_ppm / 1000000.0;
            unsigned arrivals = unsigned(source_phase); source_phase -= arrivals;
            const unsigned stall_phase = tick % 9973;
            if (stall_phase == 1234 || stall_phase == 1235) {
                held += arrivals; arrivals = 0;
            } else {
                arrivals += held; held = 0;
            }
            for (unsigned i = 0; i < arrivals; ++i) {
                const bool bad = ((tick * 17 + i * 43) % 1000) < 18;
                assert(jitter.push(bad ? silence : speech));
                radio_silence += bad;
            }
            const auto action = jitter.pop(AudioCodec::msbc, output, trimmed);
            trims += trimmed;
            if (action == EncodedPlayout::wait) {
                ++startup_wait; consecutive_conceal = 0;
            } else if (action == EncodedPlayout::conceal) {
                ++concealed; longest_conceal = std::max(longest_conceal, ++consecutive_conceal);
            } else {
                assert(action == EncodedPlayout::audio); consecutive_conceal = 0;
            }
            assert(jitter.size() <= 12);
        }
        assert(startup_wait <= 6 && radio_silence > 1000);
        assert(longest_conceal <= 1); // No post-start mute burst longer than 7.5 ms.
        if (drift_ppm < 0) assert(concealed > 0);
        if (drift_ppm > 0) assert(trims > 0);
    }
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
    contact_parser_tests();
    music_transport_tests();
    music_control_tests();
    resampler_tests();
    encoded_playout_tests();
    State a;
    a.linked = 1; a.audio = 0xdeadbeef; a.generation = 0x12345678; a.setup = 1;
    a.service = 1; a.signal = 4; a.battery = 5; a.incoming = 1; a.chld = 0x3f;
    a.number = "+491234"; a.current_count = 2;
    a.current[0] = {1, 1, 4, 0, "+491234"};
    a.current[1] = {2, 0, 0, 1, "5550100"};
    State b;
    assert(decode_state(encode_state(a), "", b) && b.audio == a.audio && b.generation == a.generation);
    assert(b.number == a.number && b.current_count == 2 && b.current[0].number == "+491234");
    assert(b.current[1].index == 2 && b.current[1].multiparty == 1);
    for (const auto &bad : {"", "1 2", "1 0 0 0 4 0 1 4 0 5 1", "1 4294967296 0 0 0 0 0 0 0 0 0",
                           "1 0 0 0 0 0 0 0 0 0 0 junk", "1 0 0 0 0 0 0 0 0 0 -1"})
        assert(!decode_state(bad, "", b));
    assert(!decode_state(encode_state(a), "x\r\nATD123;", b));
    std::string chld;
    assert(chld_argument("+CHLD=3", chld) && chld == "3");
    assert(chld_argument("+CHLD=12", chld) && chld == "12");
    assert(chld_argument("+CHLD=24", chld) && chld == "24");
    for (const auto &invalid : {"CHLD=3", "+CHLD=", "+CHLD=5", "+CHLD=10", "+CHLD=2999", "+CHLD=2x"})
        assert(!chld_argument(invalid, chld));
    assert(command_allowed(a, "answer", ""));
    assert(!command_allowed(a, "dial", "123"));
    assert(!command_allowed(a, "answer", "extra"));
    assert(!command_allowed(a, "dtmf", "1"));
    a.call = 1; a.setup = 0;
    assert(command_allowed(a, "hangup", ""));
    assert(command_allowed(a, "dtmf", "#"));
    assert(!command_allowed(a, "dtmf", "12"));
    a.held = 1; a.call = 1; a.setup = 0;
    assert(!command_allowed(a, "hangup", ""));
    assert(command_allowed(a, "chld", "0"));
    assert(command_allowed(a, "chld", "1"));
    assert(command_allowed(a, "chld", "2"));
    assert(command_allowed(a, "chld", "3"));
    assert(command_allowed(a, "chld", "12"));
    assert(command_allowed(a, "chld", "22"));
    assert(!command_allowed(a, "chld", "13"));
    assert(!command_allowed(a, "chld", "4")); // Not advertised in 0x3f.
    a.held = 0; a.linked = 0; assert(!command_allowed(a, "hangup", ""));
    State idle; idle.linked = 1;
    assert(command_allowed(idle, "dial", "+491234"));
    for (const auto &invalid : {"", "+", "+49+12", "123;ATD456", "123\r\n", "123;"})
        assert(!command_allowed(idle, "dial", invalid));
    assert(command_allowed(idle, "redial", ""));
    assert(!command_allowed(idle, "redial", "123"));
    idle.setup = 1;
    assert(!command_allowed(idle, "dial", "123") && !command_allowed(idle, "redial", ""));
    CommandGate gate; gate.reset(10);
    assert(!gate.accept(9, 1)); assert(!gate.accept(10, 0)); assert(gate.accept(10, 5));
    assert(!gate.accept(10, 5)); assert(!gate.accept(10, 4)); assert(gate.accept(10, 6));
    gate.reset(11); assert(!gate.accept(10, 7)); assert(gate.accept(11, 1));
    Controller controller;
    State remote; remote.linked = 1; remote.setup = 1; remote.number = "+491234";
    assert(controller.apply_remote_snapshot(0, 1, remote) == SnapshotResult::rejected);
    assert(controller.apply_remote_snapshot(10, 1, remote) == SnapshotResult::new_session);
    assert(controller.remote_boot() == 10 && controller.remote().number == "+491234");
    assert(controller.commands().accept(10, 1));
    remote.setup = 0; remote.call = 1;
    assert(controller.apply_remote_snapshot(10, 1, remote) == SnapshotResult::rejected);
    assert(controller.remote().setup == 1); // Duplicate snapshots cannot roll state forward.
    assert(controller.apply_remote_snapshot(10, 2, remote) == SnapshotResult::accepted);
    assert(controller.remote().call == 1);
    assert(controller.apply_remote_snapshot(11, 1, {}) == SnapshotResult::new_session);
    assert(!controller.commands().accept(10, 2) && controller.commands().accept(11, 1));
    controller.clear_remote_state();
    assert(!controller.remote().linked && controller.remote_boot() == 11);
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
    EncodedAudio encoded;
    encoded.codec = AudioCodec::msbc; encoded.size = 57;
    for (unsigned i = 0; i < encoded.size; ++i) encoded.data[i] = uint8_t(i * 7);
    auto encoded_frame = encoded_audio_frame(0x10203040, 0x50607080, 42, encoded);
    EncodedAudioDecoder encoded_decoder;
    unsigned encoded_received = 0;
    for (auto c : encoded_frame) encoded_decoder.feed(c, [&](const EncodedFrame &f) {
        assert(get32(f.data() + 4) == 0x10203040 && get32(f.data() + 8) == 0x50607080);
        assert(f[14] == uint8_t(AudioCodec::msbc) && f[15] == 57);
        assert(!memcmp(f.data() + 16, encoded.data.data(), encoded.size)); ++encoded_received;
    });
    assert(encoded_received == 1);
    auto encoded_corrupt = encoded_frame; encoded_corrupt[20] ^= 1;
    for (auto c : encoded_corrupt) encoded_decoder.feed(c, [&](const EncodedFrame &) { ++encoded_received; });
    assert(encoded_received == 1 && encoded_decoder.crc_failures() == 1);
    for (int i = 0; i < 100000; ++i) encoded_decoder.feed(uint8_t(random()), [](const EncodedFrame &) {});
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
    // The typed control channel must preserve the call domain and state payload.
    dashlink::Call m{33, 7, "state", encode_state(a), {}, {}};
    auto wire = dashlink::encode(m); dashlink::Decoder d;
    bool delivered = false;
    for (auto c : wire) d.feed(&c, 1, [&](dashlink::Packet packet) {
        const auto &out = std::get<dashlink::Call>(packet);
        assert(out.session == 33 && out.sequence == 7 && out.payload == m.payload); delivered = true;
    });
    assert(delivered);
    std::cout << "Call control, codec-transparent transport, jitter buffer, PCM fallback and rate conversion tests passed\n";
}
