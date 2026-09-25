"""Exercise production audio callbacks at both negotiated rates without Bluetooth hardware."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
source = (root / "firmware/adapters/calls/esp_hfp_adapter/call_audio.cpp").read_text()
# Keep the actual state management and input/output callbacks; replace only RTOS services.
callbacks = source[source.index("namespace runtime {"):source.index("static void worker(")]
start = source.index("std::string call_audio_diagnostics()")
callbacks += source[start:source.index("\n}", start) + 2] + "\n}\n"
harness = r'''
#include "dashbridge/protocols/calls_v3.hpp"
#include "dashbridge/adapters/call_resampler.hpp"
#include "dashbridge/adapters/call_audio_test.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
using portMUX_TYPE = int;
using QueueHandle_t = int;
constexpr int portMUX_INITIALIZER_UNLOCKED = 0, pdTRUE = 1;
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
static int64_t clock_us = 100000;
int64_t esp_timer_get_time() { return clock_us; }
static std::vector<calls::Frame> queued_frames;
int xQueueSend(QueueHandle_t, const void *packet, int) {
    // Frame is the first member of production Packet, copied by the RTOS queue.
    queued_frames.push_back(*static_cast<const calls::Frame *>(packet)); return pdTRUE;
}
'''
harness += callbacks
harness += r'''
int main() {
    using namespace runtime;
    (void)uart_events; // RTOS event queue is exercised on hardware, outside these PCM callbacks.
    for (unsigned source_rate : {8000u, 16000u}) for (unsigned target_rate : {8000u, 16000u}) {
        queued_frames.clear(); call_audio_set(0, 0, 0); call_audio_set(10, 20, source_rate);
        constexpr unsigned count = 40; // 300 ms of speech, crossing many frames.
        const unsigned input_bytes = source_rate * 2 * count * 75 / 10000;
        std::vector<uint8_t> input(input_bytes);
        for (unsigned i = 0; i < input_bytes / 2; ++i)
            calls::pcm_write(input.data() + i * 2, int16_t(12000 * std::sin(2 * 3.141592653589793 * 1000 * i / source_rate)));
        // Deliberately split input into single samples, smaller than either SDK callback.
        for (unsigned i = 0; i < input_bytes; i += 2) call_audio_in(input.data() + i, 2);
        assert(queued_frames.size() == count);
        for (const auto &frame : queued_frames) {
            assert(calls::get32(frame.data() + 4) == 10 && calls::get32(frame.data() + 8) == 20);
            assert(frame[2] == 2 && frame[3] == 240);
        }
        call_audio_set(20, 10, target_rate);
        std::vector<uint8_t> output;
        // Queue two frames to prime the real jitter buffer, then drain one frame per tick.
        for (unsigned i = 0; i < count; ++i) {
            assert(playback.push(queued_frames[i].data() + 14, calls::pcm_size));
            last_rx = clock_us; ++received;
            if (!i) continue;
            std::array<uint8_t, 240> buffer{};
            unsigned bytes = target_rate == 8000 ? 60 : 240;
            unsigned callbacks_per_frame = target_rate == 8000 ? 2 : 1;
            for (unsigned part = 0; part < callbacks_per_frame; ++part) {
                assert(call_audio_out(buffer.data(), bytes) == bytes);
                output.insert(output.end(), buffer.begin(), buffer.begin() + bytes);
            }
            clock_us += 7500;
        }
        assert(output.size() == (count - 1) * (target_rate == 8000 ? 120 : 240));
        // Check pitch and level after the filters settle, across every rate pairing.
        double energy = 0;
        unsigned crossings = 0;
        for (size_t i = 200; i < output.size(); i += 2) {
            double x = calls::pcm_read(output.data() + i); energy += x * x;
            if (x >= 0 && calls::pcm_read(output.data() + i - 2) < 0) ++crossings;
        }
        double rms = std::sqrt(energy / ((output.size() - 200) / 2));
        assert(rms > 7900 && rms < 8900);
        double seconds = double(output.size() - 200) / (2 * target_rate);
        assert(std::abs(crossings / seconds - 1000) < 10);
        if (source_rate == 16000 && target_rate == 16000)
            assert(std::equal(output.begin(), output.end(), input.begin())); // HD bypass is bit-exact.
        std::array<uint8_t, 240> buffer{};
        assert(call_audio_out(buffer.data(), 3) == 0); // Reject malformed callback sizes.
        clock_us += 60001;
        assert(call_audio_out(buffer.data(), 120) == 0); // Stale audio cannot replay.
        call_audio_set(0, 0, 0);
        assert(call_audio_out(buffer.data(), 120) == 0);
        auto before = queued_frames.size(); call_audio_in(input.data(), 120); assert(queued_frames.size() == before);
        std::cout << "PASS production callbacks " << source_rate << " -> wire 16000 -> " << target_rate
                  << " Hz: duration, pitch, level, stale audio and disconnect\n";
    }
    // IDF drains the callback until it returns zero for EVERY data-ready event.
    // An empty probe is normal and must not restart prefill or the CVSD filter.
    for (unsigned rate : {16000u, 8000u}) {
        call_audio_set(0, 0, 0); call_audio_set(30, 40, rate);
        std::vector<uint8_t> wire(40 * calls::pcm_size), actual;
        for (size_t i = 0; i < wire.size() / 2; ++i)
            calls::pcm_write(wire.data() + 2 * i, int16_t(12000 * std::sin(2 * 3.141592653589793 * 1000 * i / 16000)));
        for (unsigned tick = 0; tick < 40; ++tick) {
            assert(playback.push(wire.data() + tick * calls::pcm_size, calls::pcm_size));
            last_rx = clock_us;
            std::array<uint8_t, 240> data{};
            const unsigned request = rate == 8000 ? 60 : 240;
            unsigned delivered = 0;
            for (unsigned probe = 0; probe < 10; ++probe) {
                auto n = call_audio_out(data.data(), request);
                if (!n) break;
                delivered += n;
                actual.insert(actual.end(), data.begin(), data.begin() + n);
                assert(probe < 9); // Must terminate rather than synthesize unbounded data.
            }
            const unsigned one_frame = rate == 8000 ? 120 : 240;
            assert(delivered == (tick == 0 ? 0 : (tick == 1 ? 2 * one_frame : one_frame)));
            clock_us += 7500;
        }
        std::vector<uint8_t> expected(wire.size() / (rate == 8000 ? 2 : 1));
        if (rate == 8000) {
            calls::PcmDownsampler reference;
            reference.convert(wire.data(), wire.size(), expected.data());
        } else expected = wire;
        assert(actual == expected); // Empty SDK probes must not introduce periodic filter transients.
        std::cout << "PASS SDK drain-until-empty: " << rate << " Hz continuous PCM after one prefill\n";
    }
    using Mode = calls::AudioTestMode;
    call_audio_set(0, 0, 0);
    assert(!call_audio_test(Mode::tone)); // Never arm a later, unrelated call.
    for (unsigned rate : {8000u, 16000u}) {
        call_audio_set(101, 0, rate); // Deliberately no other board audio token.
        assert(call_audio_test(Mode::tone));
        std::vector<uint8_t> tone;
        const unsigned bytes = rate == 8000 ? 120 : 240;
        const unsigned request = rate == 8000 ? 60 : 240;
        std::array<uint8_t, 240> data{};
        auto queued_before = queued_frames.size();
        for (unsigned tick = 0; tick < 80; ++tick) {
            assert(audio_test.tick(clock_us));
            unsigned delivered = 0;
            for (unsigned probe = 0; probe < 5; ++probe) {
                auto n = call_audio_out(data.data(), request);
                if (!n) break;
                delivered += n; tone.insert(tone.end(), data.begin(), data.begin() + n);
                assert(probe < 4);
            }
            assert(delivered == bytes);
            assert(!audio_test.tick(clock_us)); // Same instant cannot create another frame.
            call_audio_in(data.data(), request); // Local received speech cannot leak to wire.
            assert(queued_frames.size() == queued_before);
            clock_us += 7500;
        }
        assert(tone.size() == 80 * bytes && audio_test.late() == 0 && audio_test.overruns() == 0);
        unsigned crossings = 0;
        for (size_t i = 2; i < tone.size(); i += 2) {
            auto sample = calls::pcm_read(tone.data() + i);
            assert(std::abs(int(sample)) <= 2048);
            if (sample >= 0 && calls::pcm_read(tone.data() + i - 2) < 0) ++crossings;
        }
        assert(crossings >= 598 && crossings <= 600); // 1 kHz for 600 ms.
        // A stalled worker emits at most one frame, never a burst of stale tones.
        clock_us += 100000; assert(audio_test.tick(clock_us));
        unsigned delivered = 0;
        while (auto n = call_audio_out(data.data(), request)) { delivered += n; assert(delivered <= bytes); }
        assert(audio_test.late() == 1);
        assert(call_audio_test(Mode::loopback));
        for (unsigned frame = 0; frame < 40; ++frame) {
            std::array<uint8_t, 240> input{}, output{};
            for (unsigned i = 0; i < bytes / 2; ++i)
                calls::pcm_write(input.data() + i * 2, int16_t(int(i * 411 + frame * 917) % 60000 - 30000));
            call_audio_in(input.data(), bytes);
            assert(queued_frames.size() == queued_before);
            for (unsigned i = 0; i < bytes; i += request) assert(call_audio_out(output.data() + i, request) == request);
            for (unsigned i = 0; i < bytes; i += 2)
                assert(calls::pcm_read(output.data() + i) == calls::pcm_read(input.data() + i) / 4);
            assert(call_audio_out(data.data(), request) == 0);
            clock_us += 7500;
        }
        assert(call_audio_out(data.data(), 3) == 0);
        call_audio_in(data.data(), bytes); clock_us += 60001;
        assert(call_audio_out(data.data(), request) == 0); // No stale echoed speech.
        assert(call_audio_test(Mode::tone)); audio_test.tick(clock_us);
        clock_us += 60000000;
        assert(call_audio_out(data.data(), request) == 0 && audio_test.mode() == Mode::normal);
        assert(call_audio_test(Mode::tone)); audio_test.tick(clock_us);
        call_audio_set(0, 0, 0);
        assert(audio_test.mode() == Mode::normal && call_audio_out(data.data(), request) == 0);
        call_audio_set(102, 202, rate);
        assert(call_audio_test(Mode::tone)); audio_test.tick(clock_us);
        // Normal playback buffered before/during a test must not be replayed on exit.
        playback.push(data.data(), 240); playback.push(data.data(), 240); last_rx = clock_us;
        assert(call_audio_test(Mode::normal));
        assert(call_audio_out(data.data(), request) == 0);
        call_audio_in(data.data(), rate == 8000 ? 120 : 240);
        assert(queued_frames.size() == queued_before + 1); // Normal wire relay resumes.
        assert(call_audio_test(Mode::loopback));
        for (unsigned i = 0; i < 30; ++i) call_audio_in(data.data(), bytes);
        assert(audio_test.overruns() > 0); // Consumer starvation stays bounded.
        call_audio_set(103, 202, rate); assert(audio_test.mode() == Mode::normal);
        std::cout << "PASS isolation tests " << rate << " Hz: paced tone, attenuated loopback, wire bypass, expiry, disconnect, bounded backlog and normal recovery\n";
    }

}
'''
with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary)
    (path / "test.cpp").write_text(harness)
    subprocess.run([os.environ.get("CXX", "clang++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I", str(root / "firmware/adapters/dashbridge_adapter_api/include"),
                    "-I", str(root / "firmware/adapters/calls/esp_hfp_adapter/include"),
                    "-I", str(root / "firmware/core/dashbridge_domain_core/include"),
                    "-I", str(root / "firmware/protocols/calls_v3/include"),
                    str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
