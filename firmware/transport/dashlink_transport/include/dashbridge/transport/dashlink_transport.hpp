#pragma once

#include "dashbridge/protocols/dashlink_v2.hpp"
#include <deque>

namespace dashbridge::transport {

class ControlQueue {
  public:
    static constexpr size_t capacity = 32;
    bool push(const protocols::dashlink_v2::Packet &packet);
    bool pop(protocols::dashlink_v2::Bytes &frame);
    size_t queued() const { return priority_.size() + normal_.size(); }

  private:
    std::deque<protocols::dashlink_v2::Bytes> priority_;
    std::deque<protocols::dashlink_v2::Bytes> normal_;
};

// In a single-board build callbacks are still deferred to the app task.
// Heartbeats cannot fill the queue; resets supersede stale notifications.
class LocalBridge {
  public:
    using Packet = protocols::dashlink_v2::Packet;
    static constexpr size_t capacity = 8;
    bool send_to_car(const Packet &packet);
    bool send_to_phone(const Packet &packet);
    bool pop_for_phone(Packet &packet);
    bool pop_for_car(Packet &packet);
    size_t queued() const { return to_car_.size(); }

  private:
    using Heartbeat = protocols::dashlink_v2::Heartbeat;
    std::deque<Packet> to_car_;
    Packet to_phone_{Heartbeat{protocols::dashlink_v2::Board::car, 0, 0}};
    bool phone_pending_ = false;
};

} // namespace dashbridge::transport
