#pragma once

#include "bridge_core.hpp"
#include <cstdint>
#include <cstdio>
#include <string>

namespace music {
struct State {
    uint32_t session = 0;
    uint32_t revision = 0;
    uint32_t length_ms = 0;
    uint32_t position_ms = 0;
    uint8_t playback = 0xff;
    std::string title, artist, album, track, track_count, genre;
};

inline bridge::WireMessage state_message(const State &state) {
    bridge::WireMessage message{bridge::Op::media_state, state.session, {}};
    message.notice.id = state.revision;
    message.notice.title = state.title;
    message.notice.subtitle = state.artist;
    message.notice.app = state.album;
    char header[80];
    snprintf(header, sizeof header, "%u,%lu,%lu\n", unsigned(state.playback),
             (unsigned long)state.length_ms, (unsigned long)state.position_ms);
    message.notice.body = std::string(header) + state.track + "\n" + state.track_count + "\n" + state.genre;
    return message;
}

inline bool decimal(const std::string &text, uint32_t &value) {
    if (text.empty()) return false;
    uint64_t result = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        result = result * 10 + unsigned(c - '0');
        if (result > 0xffffffffu) return false;
    }
    value = uint32_t(result);
    return true;
}

inline bool parse_state(const bridge::WireMessage &message, State &state) {
    if (message.op != bridge::Op::media_state || !message.session) return false;
    const auto first = message.notice.body.find('\n');
    const auto comma1 = message.notice.body.find(',');
    const auto comma2 = comma1 == std::string::npos ? comma1 : message.notice.body.find(',', comma1 + 1);
    if (first == std::string::npos || comma1 == std::string::npos || comma2 == std::string::npos ||
        comma1 > first || comma2 > first) return false;
    uint32_t playback = 0, length = 0, position = 0;
    if (!decimal(message.notice.body.substr(0, comma1), playback) || playback > 0xff ||
        !decimal(message.notice.body.substr(comma1 + 1, comma2 - comma1 - 1), length) ||
        !decimal(message.notice.body.substr(comma2 + 1, first - comma2 - 1), position)) return false;
    const auto second = message.notice.body.find('\n', first + 1);
    const auto third = second == std::string::npos ? second : message.notice.body.find('\n', second + 1);
    if (second == std::string::npos || third == std::string::npos) return false;
    state.session = message.session;
    state.revision = message.notice.id;
    state.playback = uint8_t(playback);
    state.length_ms = length;
    state.position_ms = position;
    state.title = message.notice.title;
    state.artist = message.notice.subtitle;
    state.album = message.notice.app;
    state.track = message.notice.body.substr(first + 1, second - first - 1);
    state.track_count = message.notice.body.substr(second + 1, third - second - 1);
    state.genre = message.notice.body.substr(third + 1);
    return true;
}

inline bridge::WireMessage command_message(uint32_t session, uint32_t sequence, uint8_t key, uint8_t state) {
    bridge::WireMessage message{bridge::Op::media_command, session, {}};
    message.notice.id = sequence;
    message.notice.body = std::to_string(unsigned(key)) + "," + std::to_string(unsigned(state));
    return message;
}

inline bool parse_command(const bridge::WireMessage &message, uint8_t &key, uint8_t &state) {
    if (message.op != bridge::Op::media_command || !message.session || !message.notice.id) return false;
    const auto comma = message.notice.body.find(',');
    if (comma == std::string::npos || message.notice.body.find(',', comma + 1) != std::string::npos) return false;
    uint32_t parsed_key = 0, parsed_state = 0;
    if (!decimal(message.notice.body.substr(0, comma), parsed_key) || parsed_key > 0x7f ||
        !decimal(message.notice.body.substr(comma + 1), parsed_state) || parsed_state > 1) return false;
    key = uint8_t(parsed_key);
    state = uint8_t(parsed_state);
    return true;
}
} // namespace music
