#pragma once
#include "bridge_core.hpp"
#include <deque>
#include <utility>

namespace runtime {
enum class Peer { phone, car };

// Pairing either transport must not close the other transport's window.
class PairingWindows {
    int64_t until_[2]{};
public:
    void open(Peer peer, int64_t now) { until_[int(peer)] = now + 120000; }
    bool allowed(Peer peer, int64_t now) const { return now < until_[int(peer)]; }
    void paired(Peer peer) { until_[int(peer)] = 0; }
};

// Calls between Bluetooth callbacks are deferred to the main task. Heartbeats
// cannot fill the queue; resets always supersede data from the old session.
class LocalBridge {
    std::deque<bridge::WireMessage> to_car_;
    bridge::WireMessage to_phone_{bridge::Op::heartbeat, 0, {}};
    bool phone_pending_ = false;
public:
    static constexpr size_t capacity = 8;
    bool send_to_car(const bridge::WireMessage &message) {
        if (message.op == bridge::Op::reset) to_car_.clear();
        if (message.op == bridge::Op::heartbeat) {
            for (auto &pending : to_car_) {
                if (pending.op == bridge::Op::heartbeat) {
                    pending = message;
                    return true;
                }
            }
        }
        if (to_car_.size() >= capacity) return false;
        to_car_.push_back({message.op, message.session, bridge::bounded_notice(message.notice)});
        return true;
    }
    void send_to_phone(const bridge::WireMessage &message) {
        to_phone_ = message;
        phone_pending_ = true;
    }
    bool pop_for_phone(bridge::WireMessage &message) {
        if (!phone_pending_) return false;
        message = std::move(to_phone_);
        phone_pending_ = false;
        return true;
    }
    bool pop_for_car(bridge::WireMessage &message) {
        if (to_car_.empty()) return false;
        message = std::move(to_car_.front());
        to_car_.pop_front();
        return true;
    }
    size_t queued() const { return to_car_.size(); }
};
} // namespace runtime
