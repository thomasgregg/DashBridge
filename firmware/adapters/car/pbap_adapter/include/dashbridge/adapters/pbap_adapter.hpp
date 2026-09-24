#pragma once

#include "dashbridge/core/contacts.hpp"
#include "dashbridge/protocols/obex.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace dashbridge::adapters::car::pbap {

using Bytes = dashbridge::protocols::obex::Bytes;

// Read-only PBAP projection of the atomically committed contact store.
class Server {
    dashbridge::core::contacts::Store &phonebook_;
    bool connected_ = false, response_active_ = false;
    uint16_t mtu_ = 1024;
    std::string folder_, prefix_, suffix_, fragment_;
    size_t prefix_at_ = 0, suffix_at_ = 0, fragment_at_ = 0, selected_at_ = 0;
    std::vector<uint16_t> selected_;
    uint8_t response_kind_ = 0, format_ = 0;
    Bytes pending_headers_;
    Bytes get(const Bytes &headers);
    Bytes chunk(Bytes headers = {});
    bool append_body(Bytes &body, size_t capacity);
    void begin_response(uint8_t kind, std::vector<uint16_t> selected, uint8_t format,
                        std::string prefix = {}, std::string suffix = {});

  public:
    explicit Server(dashbridge::core::contacts::Store &phonebook) : phonebook_(phonebook) {}
    static bool accepts_connect(const Bytes &packet);
    Bytes request(const Bytes &packet);
    void reset();
};

} // namespace dashbridge::adapters::car::pbap
