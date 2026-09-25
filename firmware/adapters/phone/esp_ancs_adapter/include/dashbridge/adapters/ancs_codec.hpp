#pragma once

#include "dashbridge/core/messages.hpp"
#include "dashbridge/core/setup.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace dashbridge::adapters::ancs {

using Bytes = std::vector<uint8_t>;

class NotificationResponse {
    Bytes data_;

  public:
    void clear() { data_.clear(); }
    // Returns 1 on completion, 0 when incomplete, -1 when malformed or oversized.
    int feed(const uint8_t *data, size_t size, uint32_t expected_id,
             core::messages::Message &result);
};

class ApplicationIdResponse {
    Bytes data_;

  public:
    void clear() { data_.clear(); }
    int feed(const uint8_t *data, size_t size, uint32_t expected_id, std::string &application_id);
};

class ApplicationNameResponse {
    Bytes data_;

  public:
    void clear() { data_.clear(); }
    int feed(const uint8_t *data, size_t size, const std::string &expected_id, std::string &name);
};

core::messages::Message apply_preview(core::messages::Message message,
                                      const core::setup::ApplicationRule &rule);

// Apple ANCS EventFlagPreExisting is bit 2. Bit 4 is NegativeAction
// (for example, Dismiss), and must not suppress a new notification.
constexpr bool is_preexisting(uint8_t flags) { return (flags & (1u << 2)) != 0; }

} // namespace dashbridge::adapters::ancs
