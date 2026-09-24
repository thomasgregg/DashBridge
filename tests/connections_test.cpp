#include "dashbridge/core/connections.hpp"
#include <cassert>
#include <iostream>

using Coordinator = dashbridge::core::connections::Coordinator;
using Profile = dashbridge::core::connections::Profile;

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

    std::cout << "PASS one-at-a-time Classic profile connection coordination and base-link reset\n";
}
