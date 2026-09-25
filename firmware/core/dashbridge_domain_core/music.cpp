#include "dashbridge/core/music.hpp"

#include <utility>

namespace dashbridge::core::music {
namespace {
bool metadata_valid(const std::string &value) {
    return value.size() <= maximum_metadata_length;
}
}

bool valid(const State &state) {
    return state.session && state.revision &&
           metadata_valid(state.title) && metadata_valid(state.artist) &&
           metadata_valid(state.album) && metadata_valid(state.track) &&
           metadata_valid(state.track_count) && metadata_valid(state.genre);
}

bool Controller::begin_local_session(uint32_t session) {
    if (!session) return false;
    local_ = {};
    local_.session = session;
    local_.revision = 1;
    return true;
}

StateChanges Controller::apply_remote_state(State state) {
    StateChanges changes;
    if (!valid(state) || (remote_.session == state.session && state.revision < remote_.revision))
        return changes;
    changes.accepted = true;
    changes.new_session = remote_.session != state.session;
    changes.track = changes.new_session || remote_.revision != state.revision;
    changes.playback = changes.new_session || remote_.playback != state.playback;
    changes.position = changes.new_session || remote_.position_ms != state.position_ms;
    remote_ = std::move(state);
    return changes;
}

void Controller::clear_remote_state() {
    remote_ = {};
}

bool Controller::accept_remote_command(uint32_t session, uint32_t sequence) {
    if (!session || !sequence) return false;
    if (command_session_ != session) {
        command_session_ = session;
        last_command_ = 0;
    }
    if (sequence <= last_command_) return false;
    last_command_ = sequence;
    return true;
}

AudioOwner Controller::audio_owner() const {
    if (call_active_) return AudioOwner::call;
    if (music_streaming_) return AudioOwner::music;
    return AudioOwner::none;
}

} // namespace dashbridge::core::music
