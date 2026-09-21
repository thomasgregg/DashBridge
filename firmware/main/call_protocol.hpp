#pragma once
#include "bridge_core.hpp"
#include <array>
#include <cstdio>
#include <cstring>

// Pure protocol code, shared by the firmware and sanitizer-backed host tests.
namespace calls {
constexpr const char *protocol = "calls/2";
struct State {
    unsigned linked = 0, audio = 0, generation = 0;
    unsigned call = 0, setup = 0, held = 0, service = 0, signal = 0, roam = 0, battery = 0, incoming = 0;
    std::string number;
};
inline bool number_valid(const std::string &s) {
    if (s.size() > 40) return false;
    for (char c : s) if ((c < '0' || c > '9') && c != '+' && c != '*' && c != '#') return false;
    return true;
}
inline bool decimal(const std::string &s, uint32_t &value) {
    if (s.empty() || s.size() > 10) return false;
    uint64_t n = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        n = n * 10 + unsigned(c - '0');
        if (n > UINT32_MAX) return false;
    }
    value = uint32_t(n);
    return true;
}
inline std::string encode_state(const State &s) {
    char b[180];
    snprintf(b, sizeof b, "%u %u %u %u %u %u %u %u %u %u %u", s.linked, s.audio, s.generation,
             s.call, s.setup, s.held, s.service, s.signal, s.roam, s.battery, s.incoming);
    return b;
}
inline bool decode_state(const std::string &body, const std::string &number, State &s) {
    std::array<uint32_t, 11> v{};
    size_t start = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        size_t end = body.find(' ', start);
        if ((end == std::string::npos) != (i == v.size() - 1)) return false;
        if (!decimal(body.substr(start, end == std::string::npos ? end : end - start), v[i])) return false;
        start = end + 1;
    }
    if (v[0] > 1 || v[3] > 1 || v[4] > 3 || v[5] > 2 || v[6] > 1 || v[7] > 5 || v[8] > 1 ||
        v[9] > 5 || v[10] > 1 || !number_valid(number)) return false;
    if (!v[0] && (v[1] || v[2] || v[3] || v[4] || v[5])) return false;
    s = {v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], number};
    return true;
}
inline bool command_allowed(const State &s, const std::string &command, const std::string &arg) {
    if (!s.linked || s.held) return false;
    if (command == "dial") {
        if (s.call || s.setup || arg.empty() || !number_valid(arg)) return false;
        bool digit = false;
        for (size_t i = 0; i < arg.size(); ++i) {
            if (arg[i] == '+' && i != 0) return false;
            if (arg[i] >= '0' && arg[i] <= '9') digit = true;
        }
        return digit;
    }
    if (!s.generation) return false;
    if (command == "answer") return s.setup == 1 && !s.call && arg.empty();
    if (command == "hangup") return (s.call || s.setup) && arg.empty();
    if (command == "dtmf") return s.call && arg.size() == 1 &&
        ((arg[0] >= '0' && arg[0] <= '9') || arg[0] == '*' || arg[0] == '#' || (arg[0] >= 'A' && arg[0] <= 'D'));
    return false; // No redial, call waiting or arbitrary AT forwarding in the first milestone.
}
struct CommandGate {
    uint32_t boot = 0, last = 0;
    void reset(uint32_t peer) { boot = peer; last = 0; }
    bool accept(uint32_t peer, uint32_t sequence) {
        if (!boot || boot != peer || !sequence || sequence <= last) return false;
        last = sequence;
        return true;
    }
};
constexpr size_t pcm_size = 240; // 7.5 ms of 16 kHz mono, signed 16-bit little-endian PCM.
constexpr size_t frame_size = pcm_size + 16;
constexpr unsigned audio_baud = 460800;
static_assert(frame_size * 10 * 1000000ULL < audio_baud * 7500ULL,
              "The full-duplex UART must carry each audio frame within 7.5 ms");
using Frame = std::array<uint8_t, frame_size>;
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
        if (overflow) clear(); // Bound latency; never replay an old speech backlog.
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
} // namespace calls
