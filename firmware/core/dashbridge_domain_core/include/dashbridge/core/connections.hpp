#pragma once

#include <cstdint>

namespace dashbridge::core::connections {

// Supplemental Classic profiles share one controller and one peer ACL. Only
// one profile may establish a new connection at a time; established profiles
// continue operating while another profile is admitted.
enum class Profile : uint8_t { music, contacts };

class Coordinator final {
    bool base_ready_ = false;
    bool attempt_active_ = false;
    Profile active_profile_ = Profile::music;

  public:
    void base_link_changed(bool connected);
    bool try_begin(Profile profile);
    void finish(Profile profile);

    bool base_ready() const { return base_ready_; }
    bool attempt_active() const { return attempt_active_; }
    bool attempting(Profile profile) const {
        return attempt_active_ && active_profile_ == profile;
    }
};

} // namespace dashbridge::core::connections
