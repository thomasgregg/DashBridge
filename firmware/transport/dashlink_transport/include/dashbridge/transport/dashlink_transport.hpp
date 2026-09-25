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

} // namespace dashbridge::transport
