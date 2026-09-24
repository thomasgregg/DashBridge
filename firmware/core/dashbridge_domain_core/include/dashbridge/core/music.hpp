#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace dashbridge::core::music {

constexpr size_t maximum_metadata_length = 256;

struct State {
    uint32_t session = 0;
    uint32_t revision = 0;
    uint32_t length_ms = 0;
    uint32_t position_ms = 0;
    uint8_t playback = 0xff;
    std::string title, artist, album, track, track_count, genre;
};

bool valid(const State &state);

struct StateChanges {
    bool accepted = false;
    bool new_session = false;
    bool track = false;
    bool playback = false;
    bool position = false;
};

enum class AudioOwner : uint8_t { none, music, call };

class Controller {
    State local_, remote_;
    uint32_t command_session_ = 0, last_command_ = 0;
    bool call_active_ = false, music_streaming_ = false;

  public:
    State &local() { return local_; }
    const State &local() const { return local_; }
    State &remote() { return remote_; }
    const State &remote() const { return remote_; }

    bool begin_local_session(uint32_t session);
    StateChanges apply_remote_state(State state);
    void clear_remote_state();
    bool accept_remote_command(uint32_t session, uint32_t sequence);

    void set_call_active(bool active) { call_active_ = active; }
    void set_music_streaming(bool streaming) { music_streaming_ = streaming; }
    bool call_active() const { return call_active_; }
    bool allows_music_audio() const { return !call_active_; }
    AudioOwner audio_owner() const;
};

} // namespace dashbridge::core::music
