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

bool LocalBridge::send_to_car(const Packet &packet) {
    Packet canonical;
    if (!dashlink::canonicalize(packet, canonical)) return false;
    if (dashlink::supersedes_notifications(canonical)) to_car_.clear();
    if (std::holds_alternative<Heartbeat>(canonical)) {
        for (auto &pending : to_car_) {
            if (std::holds_alternative<Heartbeat>(pending)) {
                pending = std::move(canonical);
                return true;
            }
        }
    }
    if (to_car_.size() >= capacity) return false;
    to_car_.push_back(std::move(canonical));
    return true;
}

bool LocalBridge::send_to_phone(const Packet &packet) {
    if (!dashlink::canonicalize(packet, to_phone_)) return false;
    phone_pending_ = true;
    return true;
}

bool LocalBridge::pop_for_phone(Packet &packet) {
    if (!phone_pending_) return false;
    packet = std::move(to_phone_);
    phone_pending_ = false;
    return true;
}

bool LocalBridge::pop_for_car(Packet &packet) {
    if (to_car_.empty()) return false;
    packet = std::move(to_car_.front());
    to_car_.pop_front();
    return true;
}

} // namespace dashbridge::transport
