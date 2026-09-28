#include "dashbridge/core/connections.hpp"
#include <cassert>
#include <iostream>

using Coordinator = dashbridge::core::connections::Coordinator;
using Profile = dashbridge::core::connections::Profile;
using dashbridge::core::connections::should_cancel_saved_phone_link;
using dashbridge::core::connections::should_start_saved_phone_reconnect;

int main() {
    Coordinator coordinator;

    // Supplemental profiles cannot race ahead of the authenticated HFP base.
    assert(!coordinator.try_begin(Profile::music));
    coordinator.base_link_changed(true);

    // Exactly one new Classic profile connection owns the controller at once.
    assert(coordinator.try_begin(Profile::music));
    assert(coordinator.attempting(Profile::music));
    assert(!coordinator.try_begin(Profile::contacts));

    // A stale or unrelated callback cannot release another profile's slot.
    coordinator.finish(Profile::contacts);
    assert(!coordinator.try_begin(Profile::contacts));
    coordinator.finish(Profile::music);
    assert(coordinator.try_begin(Profile::contacts));

    // Losing the HFP base cancels an in-flight supplemental connection.
    coordinator.base_link_changed(false);
    assert(!coordinator.attempt_active());
    assert(!coordinator.try_begin(Profile::music));

    // A later reconnect starts from a clean deterministic state.
    coordinator.base_link_changed(true);
    assert(coordinator.try_begin(Profile::music));
    coordinator.finish(Profile::music);
    assert(coordinator.try_begin(Profile::contacts));
    coordinator.finish(Profile::contacts);

    // A deliberate replacement-pairing window cancels one old saved link or
    // connection attempt, then leaves the newly bridged phone alone.
    assert(should_cancel_saved_phone_link(true, false, true, false));
    assert(should_cancel_saved_phone_link(true, false, false, true));
    assert(!should_cancel_saved_phone_link(true, true, true, false));
    assert(!should_cancel_saved_phone_link(false, false, true, true));

    // Normal saved-peer reconnects run only outside the explicit setup window.
    assert(should_start_saved_phone_reconnect(false, false, true, false, true));
    assert(!should_start_saved_phone_reconnect(true, false, true, false, true));
    assert(!should_start_saved_phone_reconnect(false, true, true, false, true));
    assert(!should_start_saved_phone_reconnect(false, false, false, false, true));
    assert(!should_start_saved_phone_reconnect(false, false, true, true, true));
    assert(!should_start_saved_phone_reconnect(false, false, true, false, false));

    std::cout << "PASS Classic profile coordination and phone reconnect/pairing policy\n";
}
