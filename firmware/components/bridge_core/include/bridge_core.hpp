#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace bridge {
using Bytes = std::vector<uint8_t>;
constexpr size_t max_packet = 4096;
struct Notice {
    uint32_t id = 0;
    std::string app, title, subtitle, body, date;
};
enum class Op : uint8_t { reset = 1, add = 2, update = 3, remove = 4, heartbeat = 5 };
struct WireMessage {
    Op op;
    uint32_t session;
    Notice notice;
};
Bytes encode(const WireMessage &m);
class WireDecoder {
    Bytes data_;

  public:
    void feed(const uint8_t *p, size_t n, const std::function<void(const WireMessage &)> &receive);
};
class AncsResponse {
    Bytes data_;

  public:
    void clear() { data_.clear(); }
    // Returns 1 on completion, 0 when incomplete, -1 on malformed/oversize data.
    int feed(const uint8_t *p, size_t n, uint32_t expected_id, Notice &result);
};
class ObexFramer {
    Bytes data_;

  public:
    bool feed(const uint8_t *p, size_t n, const std::function<void(const Bytes &)> &receive);
    void clear() { data_.clear(); }
};
Bytes obex_packet(uint8_t code, const Bytes &headers = {});
void byte_header(Bytes &to, uint8_t tag, const Bytes &value);
void uint_header(Bytes &to, uint8_t tag, uint32_t value);
Bytes mns_connect();
Bytes mns_event(uint32_t connection_id, uint64_t handle);
bool obex_connection_id(const Bytes &packet, uint32_t &id);
struct Stored {
    uint64_t handle;
    Notice notice;
    bool read = false;
};
class Inbox {
    uint32_t session_ = 0;
    uint64_t next_ = 1;
    std::vector<Stored> messages_;

  public:
    void clear();
    // Only genuinely new adds yield a handle for an audible new-message event.
    uint64_t apply(const WireMessage &message);
    const std::vector<Stored> &messages() const { return messages_; }
    Stored *find(uint64_t handle);
    bool erase(uint64_t handle);
};
class MasServer {
    Inbox &inbox_;
    bool connected_ = false;
    bool notifications_ = false;
    uint16_t mtu_ = 1024;
    std::string folder_;
    Bytes pending_headers_, response_body_;
    size_t response_offset_ = 0;
    Bytes get(const Bytes &headers);
    Bytes chunk(Bytes headers = {});

  public:
    explicit MasServer(Inbox &inbox) : inbox_(inbox) {}
    Bytes request(const Bytes &packet);
    void reset();
    bool notifications() const { return notifications_; }
};
std::string handle_text(uint64_t value);
} // namespace bridge
