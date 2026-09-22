#pragma once

#include "call_protocol.hpp"
#include <array>
#include <cstdint>
#include <cstring>

// Codec-transparent A2DP transport shared by both boards. The Bluetooth links
// negotiate the same deliberately narrow SBC format, so the bridge never
// decodes or re-encodes music.
namespace music {
constexpr uint32_t protocol_version = 1;
constexpr size_t max_payload = 1024;
constexpr size_t header_size = 20;
constexpr size_t frame_size = header_size + max_payload + 2;
constexpr unsigned audio_baud = 921600;

struct Audio {
    uint32_t stream = 0;
    uint16_t sequence = 0;
    uint16_t frames = 0;
    uint32_t timestamp = 0;
    uint16_t size = 0;
    std::array<uint8_t, max_payload> data{};
};

using Frame = std::array<uint8_t, frame_size>;

inline size_t encode(const Audio &audio, Frame &frame) {
    if (!audio.stream || !audio.frames || !audio.size || audio.size > max_payload)
        return 0;
    frame.fill(0);
    frame[0] = 'D';
    frame[1] = 'M';
    frame[2] = protocol_version;
    frame[3] = header_size;
    calls::put32(frame.data() + 4, audio.stream);
    frame[8] = audio.sequence;
    frame[9] = audio.sequence >> 8;
    frame[10] = audio.frames;
    frame[11] = audio.frames >> 8;
    calls::put32(frame.data() + 12, audio.timestamp);
    frame[16] = audio.size;
    frame[17] = audio.size >> 8;
    // Bytes 18-19 are reserved so the format can gain flags without changing
    // packet alignment or the version-1 decoder.
    memcpy(frame.data() + header_size, audio.data.data(), audio.size);
    const size_t total = header_size + audio.size + 2;
    const uint16_t crc = calls::crc16(frame.data(), total - 2);
    frame[total - 2] = crc;
    frame[total - 1] = crc >> 8;
    return total;
}

class Decoder {
    Frame data_{};
    size_t count_ = 0;
    size_t expected_ = 0;
    uint32_t crc_failures_ = 0;
    uint32_t malformed_ = 0;

    void discard_one() {
        if (count_) {
            --count_;
            memmove(data_.data(), data_.data() + 1, count_);
        }
        expected_ = 0;
    }

  public:
    uint32_t crc_failures() const { return crc_failures_; }
    uint32_t malformed() const { return malformed_; }

    template <class Receive> void feed(uint8_t byte, Receive receive) {
        if (count_ == data_.size()) {
            ++malformed_;
            discard_one();
        }
        data_[count_++] = byte;
        while (count_ &&
               (data_[0] != 'D' || (count_ > 1 && data_[1] != 'M') ||
                (count_ > 2 && data_[2] != protocol_version) ||
                (count_ > 3 && data_[3] != header_size)))
            discard_one();
        if (count_ < header_size)
            return;
        if (!expected_) {
            const size_t payload = size_t(data_[16]) | size_t(data_[17]) << 8;
            if (!payload || payload > max_payload || data_[18] || data_[19]) {
                ++malformed_;
                discard_one();
                return;
            }
            expected_ = header_size + payload + 2;
        }
        if (count_ < expected_)
            return;
        const uint16_t crc = calls::crc16(data_.data(), expected_ - 2);
        if (data_[expected_ - 2] != uint8_t(crc) || data_[expected_ - 1] != uint8_t(crc >> 8)) {
            ++crc_failures_;
            discard_one();
            return;
        }
        Audio audio;
        audio.stream = calls::get32(data_.data() + 4);
        audio.sequence = uint16_t(data_[8]) | uint16_t(data_[9]) << 8;
        audio.frames = uint16_t(data_[10]) | uint16_t(data_[11]) << 8;
        audio.timestamp = calls::get32(data_.data() + 12);
        audio.size = uint16_t(data_[16]) | uint16_t(data_[17]) << 8;
        if (!audio.stream || !audio.frames) {
            ++malformed_;
            discard_one();
            return;
        }
        memcpy(audio.data.data(), data_.data() + header_size, audio.size);
        const size_t consumed = expected_;
        count_ -= consumed;
        if (count_)
            memmove(data_.data(), data_.data() + consumed, count_);
        expected_ = 0;
        receive(audio);
    }
};
} // namespace music
