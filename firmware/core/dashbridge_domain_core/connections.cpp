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

} // namespace dashbridge::core::connections
