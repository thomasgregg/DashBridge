#include "dashbridge/transport/dashlink_transport.hpp"
#include <utility>

namespace dashbridge::transport {
namespace dashlink = protocols::dashlink_v2;

bool ControlQueue::push(const dashlink::Packet &packet) {
    auto frame = dashlink::encode(packet);
    if (frame.empty()) return false;
    auto &queue = dashlink::is_call(packet) ? priority_ : normal_;
    if (queue.size() >= capacity) return false;
    queue.push_back(std::move(frame));
    return true;
}

bool ControlQueue::pop(dashlink::Bytes &frame) {
    auto &queue = priority_.empty() ? normal_ : priority_;
    if (queue.empty()) return false;
    frame = std::move(queue.front());
    queue.pop_front();
    return true;
}

} // namespace dashbridge::transport
