#pragma once

#include "dashbridge/core/messages.hpp"
#include "dashbridge/protocols/obex.hpp"

#include <cstdint>
#include <string>

namespace dashbridge::adapters::car::map {

using Bytes = dashbridge::protocols::obex::Bytes;

bool accepts_connect(const Bytes &packet);
std::string handle_text(uint64_t value);

class Server {
    dashbridge::core::messages::Store &inbox_;
    bool connected_ = false;
    bool notifications_ = false;
    uint16_t mtu_ = 1024;
    std::string folder_;
    Bytes pending_headers_, response_body_;
    size_t response_offset_ = 0;

    Bytes get(const Bytes &headers);
    Bytes chunk(Bytes headers = {});

  public:
    explicit Server(dashbridge::core::messages::Store &inbox) : inbox_(inbox) {}
    Bytes request(const Bytes &packet);
    void reset();
    bool notifications() const { return notifications_; }
};

Bytes notification_connect();
Bytes notification_event(uint32_t connection_id, uint64_t handle);
bool notification_connection_id(const Bytes &packet, uint32_t &connection_id);

} // namespace dashbridge::adapters::car::map
