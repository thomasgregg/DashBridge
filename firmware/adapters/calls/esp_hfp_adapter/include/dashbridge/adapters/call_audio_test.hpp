#pragma once
#include "dashbridge/adapters/call_control.hpp"
#include "dashbridge/adapters/call_resampler.hpp"
#include <array>
#include <algorithm>
#include <cstdint>

namespace calls {
inline const char *audio_test_name(AudioTestMode mode) {
    return mode == AudioTestMode::tone ? "tone" : mode == AudioTestMode::loopback ? "loopback" : "normal";
}
// Caller holds the audio lock. Local negotiated PCM rate; no wire dependency.
// Bounded output is essential: IDF drains the callback until it returns zero.
class AudioTest {
    std::array<uint8_t, 1920> data_{};
    size_t read_ = 0, size_ = 0;
    AudioTestMode mode_ = AudioTestMode::normal;
    unsigned rate_ = 0, phase_ = 0;
    int64_t deadline_ = 0, next_tone_ = 0, last_data_ = 0;
    uint32_t overruns_ = 0, late_ = 0;
    void clear_queue() { read_ = size_ = 0; last_data_ = 0; }
    void push(const uint8_t *data, size_t size, int64_t now, bool attenuate = false) {
        if (!size || size % 2 || size > data_.size()) { ++overruns_; clear_queue(); return; }
        if (size_ + size > std::min(data_.size(), size_t(rate_ * 60 / 1000)) || (last_data_ && now - last_data_ > 60000)) {
            ++overruns_; clear_queue();
        }
        for (size_t i = 0; i < size; i += 2) {
            auto index = (read_ + size_ + i) % data_.size();
            pcm_write(data_.data() + index, attenuate ? pcm_read(data + i) / 4 : pcm_read(data + i));
        }
        size_ += size; last_data_ = now;
    }
public:
    AudioTestMode mode() const { return mode_; }
    uint32_t overruns() const { return overruns_; }
    uint32_t late() const { return late_; }
    int64_t remaining_ms(int64_t now) const { return mode_ == AudioTestMode::normal ? 0 : std::max(int64_t(0), (deadline_ - now) / 1000); }
    bool start(AudioTestMode mode, unsigned rate, int64_t now) {
        if (mode != AudioTestMode::normal && rate != 8000 && rate != 16000) return false;
        mode_ = mode; rate_ = rate; phase_ = 0; clear_queue();
        overruns_ = late_ = 0; deadline_ = now + 60000000; next_tone_ = now;
        return true;
    }
    void stop() { mode_ = AudioTestMode::normal; clear_queue(); }
    bool expire(int64_t now) {
        if (mode_ == AudioTestMode::normal || now < deadline_) return false;
        stop(); return true;
    }
    void input(const uint8_t *data, size_t size, int64_t now) {
        if (mode_ == AudioTestMode::loopback) push(data, size, now, true);
    }
    bool tick(int64_t now) {
        if (mode_ == AudioTestMode::tone && now >= next_tone_) {
            // Never send a burst of overdue tones after scheduler starvation.
            if (now - next_tone_ >= 7500) { ++late_; clear_queue(); next_tone_ = now; }
            next_tone_ += 7500;
            static constexpr int16_t sine[16] = {0,784,1448,1892,2048,1892,1448,784,0,-784,-1448,-1892,-2048,-1892,-1448,-784};
            std::array<uint8_t, 240> frame{};
            const unsigned samples = rate_ * 75 / 10000;
            for (unsigned i = 0; i < samples; ++i) {
                pcm_write(frame.data() + i * 2, sine[phase_]);
                phase_ = (phase_ + (rate_ == 8000 ? 2 : 1)) % 16;
            }
            push(frame.data(), samples * 2, now);
        }
        return mode_ != AudioTestMode::normal && size_ != 0;
    }
    size_t output(uint8_t *data, size_t size, int64_t now) {
        if (mode_ == AudioTestMode::normal || !size || size % 2 || size > 240) return 0;
        if (!last_data_ || now - last_data_ > 60000) clear_queue();
        if (size_ < size) return 0;
        for (size_t i = 0; i < size; ++i) data[i] = data_[(read_ + i) % data_.size()];
        read_ = (read_ + size) % data_.size(); size_ -= size;
        return size;
    }
};
}
