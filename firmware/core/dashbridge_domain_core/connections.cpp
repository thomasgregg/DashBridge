#include "dashbridge/core/connections.hpp"

namespace dashbridge::core::connections {

void Coordinator::base_link_changed(bool connected) {
    base_ready_ = connected;
    if (!connected)
        attempt_active_ = false;
}

bool Coordinator::try_begin(Profile profile) {
    if (!base_ready_ || attempt_active_)
        return false;
    active_profile_ = profile;
    attempt_active_ = true;
    return true;
}

void Coordinator::finish(Profile profile) {
    if (attempt_active_ && active_profile_ == profile)
        attempt_active_ = false;
}

bool should_cancel_saved_phone_link(bool pairing_open, bool pairing_window_observed,
                                    bool linked, bool connecting) {
    return pairing_open && !pairing_window_observed && (linked || connecting);
}

bool should_start_saved_phone_reconnect(bool pairing_open, bool linked, bool peer_saved,
                                        bool connecting, bool deadline_reached) {
    return !pairing_open && !linked && peer_saved && !connecting && deadline_reached;
}

} // namespace dashbridge::core::connections
