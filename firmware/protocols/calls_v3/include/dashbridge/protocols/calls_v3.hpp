#pragma once
#include "dashbridge/core/calls.hpp"
#include <array>
#include <cstdio>
#include <cstring>

// Pure calls/3 and call-audio codecs shared by firmware and host tests.
namespace calls {
constexpr const char *protocol = "calls/3";
using dashbridge::core::calls::CommandGate;
using dashbridge::core::calls::Controller;
using dashbridge::core::calls::CurrentCall;
using dashbridge::core::calls::SnapshotResult;
using dashbridge::core::calls::State;
using dashbridge::core::calls::chld_feature;
using dashbridge::core::calls::command_allowed;
using dashbridge::core::calls::decimal;
using dashbridge::core::calls::max_current_calls;
using dashbridge::core::calls::number_valid;
inline std::string number_token(const std::string &number) { return number.empty() ? "-" : number; }
inline std::string encode_state(const State &s) {
    char base[220];
    snprintf(base, sizeof base, "%u %u %u %u %u %u %u %u %u %u %u %u %s %u",
             s.linked, s.audio, s.generation, s.call, s.setup, s.held, s.service, s.signal,
             s.roam, s.battery, s.incoming, s.chld, number_token(s.number).c_str(), s.current_count);
    std::string result = base;
    for (size_t i = 0; i < s.current_count && i < s.current.size(); ++i) {
        const auto &call = s.current[i];
        char fields[96];
        snprintf(fields, sizeof fields, " %u %u %u %u %s", call.index, call.direction,
                 call.status, call.multiparty, number_token(call.number).c_str());
        result += fields;
    }
    return result;
}
inline bool decode_state(const std::string &body, const std::string &number, State &s) {
    if (!number.empty()) return false; // Calls/3 carries every number in the authenticated state body.
    std::array<std::string, 14 + max_current_calls * 5> fields{};
    size_t count = 0, start = 0;
    while (start <= body.size() && count < fields.size()) {
        const size_t end = body.find(' ', start);
        fields[count++] = body.substr(start, end == std::string::npos ? end : end - start);
        if (end == std::string::npos) { start = body.size() + 1; break; }
        start = end + 1;
    }
    if (count < 14 || start <= body.size()) return false;
    std::array<uint32_t, 13> v{};
    for (size_t i = 0; i < 12; ++i)
        if (!decimal(fields[i], v[i])) return false;
    std::string primary = fields[12] == "-" ? "" : fields[12];
    if (!number_valid(primary) || !decimal(fields[13], v[12])) return false;
    if (v[0] > 1 || v[3] > 1 || v[4] > 3 || v[5] > 2 || v[6] > 1 || v[7] > 5 || v[8] > 1 ||
        v[9] > 5 || v[10] > 1 || v[11] > 0x7f || v[12] > max_current_calls) return false;
    if (!v[0] && (v[1] || v[2] || v[3] || v[4] || v[5])) return false;
    if (count != 14 + v[12] * 5) return false;
    State decoded;
    decoded.linked = v[0]; decoded.audio = v[1]; decoded.generation = v[2];
    decoded.call = v[3]; decoded.setup = v[4]; decoded.held = v[5]; decoded.service = v[6];
    decoded.signal = v[7]; decoded.roam = v[8]; decoded.battery = v[9]; decoded.incoming = v[10];
    decoded.chld = v[11]; decoded.number = primary; decoded.current_count = v[12];
    size_t at = 14;
    for (size_t i = 0; i < decoded.current_count; ++i) {
        std::array<uint32_t, 4> c{};
        for (size_t j = 0; j < c.size(); ++j)
            if (!decimal(fields[at + j], c[j])) return false;
        std::string call_number = fields[at + 4] == "-" ? "" : fields[at + 4];
        if (!c[0] || c[0] > 255 || c[1] > 1 || c[2] > 5 || c[3] > 1 || !number_valid(call_number))
            return false;
        for (size_t j = 0; j < i; ++j)
            if (decoded.current[j].index == c[0]) return false;
        decoded.current[i] = {c[0], c[1], c[2], c[3], call_number};
        at += 5;
    }
    s = std::move(decoded);
    return true;
}
inline bool chld_argument(const std::string &raw, std::string &argument) {
    constexpr const char *prefix = "+CHLD=";
    if (raw.compare(0, strlen(prefix), prefix)) return false;
    argument = raw.substr(strlen(prefix));
    const unsigned feature = chld_feature(argument);
    if (!feature) return false;
    if ((feature == 0x04 || feature == 0x10)) {
        uint32_t index = 0;
        if (!decimal(argument.substr(1), index) || !index || index > 255) return false;
    }
    return true;
}
constexpr size_t pcm_size = 240; // 7.5 ms of 16 kHz mono, signed 16-bit little-endian PCM.
constexpr size_t frame_size = pcm_size + 16;
constexpr size_t encoded_audio_size = 60; // One complete HFP mSBC/HCI payload, including optional padding.
constexpr size_t encoded_frame_size = encoded_audio_size + 18;
// The same dedicated wire now carries transparent SBC music when SCO is idle.
// 921600 has ample headroom for a standard 44.1 kHz SBC stream plus framing.
constexpr unsigned audio_baud = 921600;
static_assert(frame_size * 10 * 1000000ULL < audio_baud * 7500ULL,
              "The full-duplex UART must carry each audio frame within 7.5 ms");
using Frame = std::array<uint8_t, frame_size>;
enum class AudioCodec : uint8_t { cvsd = 1, msbc = 2 };
struct EncodedAudio {
    AudioCodec codec = AudioCodec::msbc;
    uint8_t size = 0;
    std::array<uint8_t, encoded_audio_size> data{};
};
constexpr size_t msbc_frame_size = 57;
// Espressif's pinned mSBC PLC zero-signal frame. It decodes to 7.5 ms of
// silence and preserves the SCO cadence without repeating speech.
constexpr std::array<uint8_t, msbc_frame_size> msbc_silence_frame = {
    0xad, 0x00, 0x00, 0xc5, 0x00, 0x00, 0x00, 0x00, 0x77, 0x6d,
    0xb6, 0xdd, 0xdb, 0x6d, 0xb7, 0x76, 0xdb, 0x6d, 0xdd, 0xb6,
    0xdb, 0x77, 0x6d, 0xb6, 0xdd, 0xdb, 0x6d, 0xb7, 0x76, 0xdb,
    0x6d, 0xdd, 0xb6, 0xdb, 0x77, 0x6d, 0xb6, 0xdd, 0xdb, 0x6d,
    0xb7, 0x76, 0xdb, 0x6d, 0xdd, 0xb6, 0xdb, 0x77, 0x6d, 0xb6,
    0xdd, 0xdb, 0x6d, 0xb7, 0x76, 0xdb, 0x6c,
};
inline EncodedAudio msbc_silence_audio() {
    EncodedAudio audio;
    audio.codec = AudioCodec::msbc; audio.size = msbc_silence_frame.size();
    memcpy(audio.data.data(), msbc_silence_frame.data(), msbc_silence_frame.size());
    return audio;
}
using EncodedFrame = std::array<uint8_t, encoded_frame_size>;
inline uint16_t crc16(const uint8_t *p, size_t n) {
    uint16_t c = 0xffff;
    while (n--) { c ^= uint16_t(*p++) << 8; for (int i = 0; i < 8; ++i) c = (c << 1) ^ ((c & 0x8000) ? 0x1021 : 0); }
    return c;
}
inline void put32(uint8_t *p, uint32_t n) { for (int i = 0; i < 4; ++i) p[i] = n >> (8 * i); }
inline uint32_t get32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
inline Frame audio_frame(uint32_t source, uint32_t target, uint16_t sequence, const uint8_t *pcm) {
    Frame f{};
    f[0] = 'D'; f[1] = 'A'; f[2] = 2; f[3] = pcm_size;
    put32(f.data() + 4, source); put32(f.data() + 8, target);
    f[12] = sequence; f[13] = sequence >> 8;
    memcpy(f.data() + 14, pcm, pcm_size);
    auto crc = crc16(f.data(), frame_size - 2);
    f[frame_size - 2] = crc; f[frame_size - 1] = crc >> 8;
    return f;
}
inline EncodedFrame encoded_audio_frame(uint32_t source, uint32_t target, uint16_t sequence,
                                        const EncodedAudio &audio) {
    EncodedFrame f{};
    f[0] = 'D'; f[1] = 'E'; f[2] = 1; f[3] = encoded_audio_size;
    put32(f.data() + 4, source); put32(f.data() + 8, target);
    f[12] = sequence; f[13] = sequence >> 8;
    f[14] = uint8_t(audio.codec); f[15] = audio.size;
    if (audio.size <= audio.data.size()) memcpy(f.data() + 16, audio.data.data(), audio.size);
    auto crc = crc16(f.data(), encoded_frame_size - 2);
    f[encoded_frame_size - 2] = crc; f[encoded_frame_size - 1] = crc >> 8;
    return f;
}
class AudioDecoder {
    Frame data_{};
    size_t count_ = 0;
    uint32_t crc_failures_ = 0;
public:
    uint32_t crc_failures() const { return crc_failures_; }
    template<class Receive> void feed(uint8_t c, Receive receive) {
        data_[count_++] = c;
        while (count_ && (data_[0] != 'D' || (count_ > 1 && data_[1] != 'A') ||
               (count_ > 2 && data_[2] != 2) || (count_ > 3 && data_[3] != pcm_size))) {
            --count_; memmove(data_.data(), data_.data() + 1, count_);
        }
        if (count_ != frame_size) return;
        auto crc = crc16(data_.data(), frame_size - 2);
        if (data_[frame_size - 2] == uint8_t(crc) && data_[frame_size - 1] == uint8_t(crc >> 8)) { receive(data_); count_ = 0; }
        else { ++crc_failures_; --count_; memmove(data_.data(), data_.data() + 1, count_); }
    }
};
class EncodedAudioDecoder {
    EncodedFrame data_{};
    size_t count_ = 0;
    uint32_t crc_failures_ = 0;
public:
    uint32_t crc_failures() const { return crc_failures_; }
    template<class Receive> void feed(uint8_t c, Receive receive) {
        data_[count_++] = c;
        while (count_ && (data_[0] != 'D' || (count_ > 1 && data_[1] != 'E') ||
               (count_ > 2 && data_[2] != 1) || (count_ > 3 && data_[3] != encoded_audio_size))) {
            --count_; memmove(data_.data(), data_.data() + 1, count_);
        }
        if (count_ != encoded_frame_size) return;
        auto crc = crc16(data_.data(), encoded_frame_size - 2);
        const bool valid_codec = data_[14] == uint8_t(AudioCodec::cvsd) || data_[14] == uint8_t(AudioCodec::msbc);
        if (data_[encoded_frame_size - 2] == uint8_t(crc) &&
            data_[encoded_frame_size - 1] == uint8_t(crc >> 8) && valid_codec && data_[15] <= encoded_audio_size) {
            receive(data_); count_ = 0;
        } else {
            ++crc_failures_; --count_; memmove(data_.data(), data_.data() + 1, count_);
        }
    }
};
struct AudioSequence {
    bool have = false;
    uint16_t last = 0;
    int64_t time = 0;
    // 0: duplicate/old, 1: contiguous, 2: restart/gap (clear queued PCM).
    unsigned accept(uint16_t sequence, int64_t now) {
        uint16_t delta = sequence - last;
        if (have && (!delta || (delta >= 0x8000 && now - time <= 60000))) return 0;
        unsigned result = !have || delta != 1 || now - time > 60000 ? 2 : 1;
        have = true; last = sequence; time = now;
        return result;
    }
};
// Caller supplies synchronization. Never allocate or block inside Bluetooth audio callbacks.
class PcmBuffer {
    std::array<uint8_t, pcm_size * 8> data_{};
    size_t read_ = 0, size_ = 0;
    bool primed_ = false;
public:
    void clear() { read_ = size_ = 0; primed_ = false; }
    bool push(const uint8_t *p, size_t n) {
        if (n > data_.size() || n % 2) return false;
        bool overflow = size_ + n > data_.size();
        if (overflow) clear(); // Bound latency; never replay a stale speech backlog.
        for (size_t i = 0; i < n; ++i) data_[(read_ + size_ + i) % data_.size()] = p[i];
        size_ += n;
        return !overflow;
    }
    size_t pop(uint8_t *p, size_t n) {
        if (!n || n % 2 || n > data_.size()) return 0;
        if (!primed_ && size_ < pcm_size * 2) return 0;
        // IDF drains this callback until zero on each data-ready event. An empty
        // probe is not a stream interruption: keep prefill and any partial PCM.
        // The owner clears explicitly on timeout, sequence gaps or a new stream.
        if (size_ < n) return 0;
        primed_ = true;
        for (size_t i = 0; i < n; ++i) p[i] = data_[(read_ + i) % data_.size()];
        read_ = (read_ + n) % data_.size(); size_ -= n;
        return n;
    }
};
enum class EncodedPlayout : uint8_t { wait, audio, conceal, codec_mismatch };
// Codec-transparent audio is consumed on the destination link's receive clock.
// Six frames (45 ms) are collected once at startup. Thereafter a low watermark
// requests one silence frame without draining queued speech; a high watermark
// trims stale speech so independent SCO clocks cannot grow latency indefinitely.
class EncodedJitterBuffer {
    static constexpr size_t capacity_ = 32, prefill_ = 6, low_water_ = 4, high_water_ = 12;
    std::array<EncodedAudio, capacity_> data_{};
    size_t read_ = 0, size_ = 0;
    bool started_ = false, concealed_last_ = false;
public:
    void clear() { read_ = size_ = 0; started_ = concealed_last_ = false; }
    size_t size() const { return size_; }
    bool push(const EncodedAudio &audio) {
        if (!audio.size || audio.size > encoded_audio_size) return false;
        bool overflow = size_ == capacity_;
        if (overflow) { read_ = (read_ + 1) % capacity_; --size_; }
        data_[(read_ + size_) % capacity_] = audio; ++size_;
        return !overflow;
    }
    EncodedPlayout pop(AudioCodec codec, EncodedAudio &audio, size_t &trimmed) {
        trimmed = 0;
        if (!started_ && size_ < prefill_) return EncodedPlayout::wait;
        started_ = true;
        if (size_ && data_[read_].codec != codec) { clear(); return EncodedPlayout::codec_mismatch; }
        while (size_ > high_water_) {
            read_ = (read_ + 1) % capacity_; --size_; ++trimmed;
        }
        // Never turn a short scheduling stall into consecutive silence while
        // speech remains queued. One concealment tick recovers depth; the next
        // tick consumes queued speech even if the producer is still late.
        if (size_ <= low_water_ && !concealed_last_) {
            concealed_last_ = true; return EncodedPlayout::conceal;
        }
        if (!size_) return EncodedPlayout::conceal;
        audio = data_[read_];
        read_ = (read_ + 1) % capacity_; --size_;
        concealed_last_ = false;
        return EncodedPlayout::audio;
    }
};
} // namespace calls
