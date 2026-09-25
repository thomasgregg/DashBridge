#pragma once

#include "dashbridge/core/contacts.hpp"
#include "dashbridge/core/messages.hpp"
#include "dashbridge/core/music.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <variant>
#include <vector>

namespace dashbridge::protocols::dashlink_v2 {

using Bytes = std::vector<uint8_t>;

enum class Board : uint8_t { phone = 1, car = 2 };

struct Heartbeat {
    Board source = Board::phone;
    uint32_t session = 0;
    uint32_t flags = 0;
};

struct Notification {
    dashbridge::core::messages::ChangeKind kind = dashbridge::core::messages::ChangeKind::reset;
    uint32_t session = 0;
    dashbridge::core::messages::Message message;
};

// Call control remains a versioned calls/3 payload, but its fields are named
// for that domain rather than borrowed from a notification object.
struct Call {
    uint32_t session = 0;
    uint32_t sequence = 0;
    std::string kind;
    std::string payload;
    std::string auxiliary;
    std::string destination;
};

struct MusicState { dashbridge::core::music::State state; };
struct MusicCommand {
    uint32_t session = 0;
    uint32_t sequence = 0;
    uint8_t key = 0;
    uint8_t state = 0;
};

struct ContactReset { uint32_t session = 0; };
struct ContactEntry {
    uint32_t session = 0;
    uint32_t sequence = 0;
    dashbridge::core::contacts::Entry entry;
};
struct ContactDone {
    uint32_t session = 0;
    uint32_t count = 0;
};
struct ContactAck {
    uint32_t session = 0;
    uint32_t count = 0;
    bool accepted = false;
};

using Packet = std::variant<Heartbeat, Notification, Call, MusicState, MusicCommand,
                            ContactReset, ContactEntry, ContactDone, ContactAck>;

constexpr size_t maximum_frame_size = 2048;

Bytes encode(const Packet &packet);
bool canonicalize(const Packet &input, Packet &output);

class Decoder {
    Bytes data_;
    uint32_t crc_failures_ = 0, malformed_ = 0;

  public:
    void feed(const uint8_t *data, size_t size, const std::function<void(Packet)> &receive);
    void clear() { data_.clear(); }
    uint32_t crc_failures() const { return crc_failures_; }
    uint32_t malformed() const { return malformed_; }
};

bool is_call(const Packet &packet);
bool is_phone_heartbeat(const Packet &packet);
bool is_car_heartbeat(const Packet &packet);
bool supersedes_notifications(const Packet &packet);

} // namespace dashbridge::protocols::dashlink_v2
