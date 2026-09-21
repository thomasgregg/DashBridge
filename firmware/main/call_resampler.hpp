#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace calls {
// Streaming 2:1 conversion for the CVSD fallback. The wire always carries 16 kHz
// PCM. Filter before decimation and after zero insertion to avoid aliases/images.
// 31-tap Hamming-windowed sinc, cutoff 4 kHz at 16 kHz, unity DC gain in Q15.
class HalfbandFilter {
    std::array<int16_t, 31> history_{};
    size_t next_ = 0;
public:
    void clear() { history_.fill(0); next_ = 0; }
    int16_t sample(int16_t value, int gain = 1) {
        static constexpr int16_t taps[] = {
            -56, 0, 96, 0, -221, 0, 462, 0, -878, 0, 1609, 0, -3176, 0,
            10342, 16412, 10342, 0, -3176, 0, 1609, 0, -878, 0, 462, 0,
            -221, 0, 96, 0, -56
        };
        history_[next_] = value;
        int64_t sum = 0;
        size_t index = next_;
        for (auto tap : taps) {
            if (tap) sum += int32_t(history_[index]) * tap;
            index = index ? index - 1 : history_.size() - 1;
        }
        next_ = (next_ + 1) % history_.size();
        sum = sum * gain / 32768;
        return int16_t(sum > 32767 ? 32767 : (sum < -32768 ? -32768 : sum));
    }
};
inline int16_t pcm_read(const uint8_t *p) {
    const unsigned value = unsigned(p[0]) | unsigned(p[1]) << 8;
    return int16_t(value < 32768 ? int(value) : int(value) - 65536);
}
inline void pcm_write(uint8_t *p, int16_t value) {
    const auto bits = uint16_t(value);
    p[0] = uint8_t(bits); p[1] = uint8_t(bits >> 8);
}
class PcmUpsampler {
    HalfbandFilter filter_;
public:
    void clear() { filter_.clear(); }
    // Input is whole 8 kHz int16 samples; output has twice as many bytes.
    void convert(const uint8_t *input, size_t bytes, uint8_t *output) {
        for (size_t i = 0; i < bytes; i += 2) {
            pcm_write(output + 2 * i, filter_.sample(pcm_read(input + i), 2));
            pcm_write(output + 2 * i + 2, filter_.sample(0, 2));
        }
    }
};
class PcmDownsampler {
    HalfbandFilter filter_;
public:
    void clear() { filter_.clear(); }
    // Input contains pairs of 16 kHz int16 samples; output has half the bytes.
    void convert(const uint8_t *input, size_t bytes, uint8_t *output) {
        for (size_t i = 0; i < bytes; i += 4) {
            filter_.sample(pcm_read(input + i));
            pcm_write(output + i / 2, filter_.sample(pcm_read(input + i + 2)));
        }
    }
};
}
