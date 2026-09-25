#include "dashbridge/core/music.hpp"

#include <cassert>
#include <iostream>

using namespace dashbridge::core::music;

static State state(uint32_t session, uint32_t revision) {
    State value;
    value.session = session;
    value.revision = revision;
    value.title = "Track";
    value.artist = "Artist";
    value.playback = 1;
    value.position_ms = 100;
    return value;
}

int main() {
    Controller controller;
    assert(controller.begin_local_session(7));
    assert(controller.local().session == 7 && controller.local().revision == 1);
    assert(!controller.begin_local_session(0));

    auto first = controller.apply_remote_state(state(10, 2));
    assert(first.accepted && first.new_session && first.track && first.playback && first.position);
    auto stale = controller.apply_remote_state(state(10, 1));
    assert(!stale.accepted && controller.remote().revision == 2);
    auto same = state(10, 2);
    same.position_ms = 200;
    auto position = controller.apply_remote_state(same);
    assert(position.accepted && !position.track && !position.playback && position.position);
    auto next = controller.apply_remote_state(state(11, 1));
    assert(next.accepted && next.new_session && next.track);

    auto oversized = state(12, 1);
    oversized.title.assign(maximum_metadata_length + 1, 'x');
    assert(!controller.apply_remote_state(oversized).accepted);

    assert(controller.accept_remote_command(20, 1));
    assert(!controller.accept_remote_command(20, 1));
    assert(!controller.accept_remote_command(20, 0));
    assert(controller.accept_remote_command(20, 2));
    assert(controller.accept_remote_command(21, 1));
    assert(!controller.accept_remote_command(21, 1));

    controller.set_music_streaming(true);
    assert(controller.audio_owner() == AudioOwner::music && controller.allows_music_audio());
    controller.set_call_active(true);
    assert(controller.audio_owner() == AudioOwner::call && !controller.allows_music_audio());
    controller.set_music_streaming(false);
    assert(controller.audio_owner() == AudioOwner::call);
    controller.set_call_active(false);
    assert(controller.audio_owner() == AudioOwner::none && controller.allows_music_audio());

    controller.clear_remote_state();
    assert(controller.remote().session == 0);
    std::cout << "Music state, replay protection, metadata bounds and call-priority focus tests passed\n";
}
