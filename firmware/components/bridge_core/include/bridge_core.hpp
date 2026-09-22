#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace bridge {
using Bytes = std::vector<uint8_t>;
constexpr size_t max_packet = 4096;
struct Notice {
    uint32_t id = 0;
    std::string app, title, subtitle, body, date;
};
enum class Preview : uint8_t { full = 0, sender = 1, app = 2 };
struct AppRule {
    std::string id, name;
    Preview preview = Preview::full;
};
class AppPolicy {
    std::vector<AppRule> rules_;
  public:
    static constexpr size_t limit = 12;
    AppPolicy();
    const std::vector<AppRule> &rules() const { return rules_; }
    const AppRule *find(const std::string &id) const;
    bool allow(const std::string &id, const std::string &name, Preview preview);
    bool deny(const std::string &id);
    Bytes serialize() const;
    bool load(const Bytes &data);
};
bool valid_app_id(const std::string &id);
std::string app_name_fallback(const std::string &id);
Notice apply_preview(Notice notice, const AppRule &rule);
enum class Op : uint8_t {
    reset = 1, add = 2, update = 3, remove = 4, heartbeat = 5, call = 6,
    contact_reset = 7, contact_entry = 8, contact_done = 9, contact_ack = 10,
    media_state = 11, media_command = 12, history_add = 13
};
constexpr bool contact_op(Op op) {
    return op >= Op::contact_reset && op <= Op::contact_ack;
}
struct WireMessage {
    Op op;
    uint32_t session;
    Notice notice;
};
Notice bounded_notice(const Notice &notice);
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
class AncsAppIdResponse {
    Bytes data_;
  public:
    void clear() { data_.clear(); }
    int feed(const uint8_t *p, size_t n, uint32_t expected_id, std::string &app_id);
};
class AncsAppNameResponse {
    Bytes data_;
  public:
    void clear() { data_.clear(); }
    int feed(const uint8_t *p, size_t n, const std::string &expected_id, std::string &name);
};
// Apple ANCS EventFlagPreExisting is bit 2. Bit 4 is NegativeAction
// (for example, Dismiss), and must not suppress a new notification.
constexpr bool ancs_is_preexisting(uint8_t flags) { return (flags & (1u << 2)) != 0; }
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
enum class ObexService { unknown, map, pbap };
ObexService obex_service(const Bytes &connect_packet);
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

enum class PhonebookRepository : uint8_t {
    contacts = 0, favorites = 1, incoming = 2, outgoing = 3, missed = 4, combined = 5
};
struct PhonebookEntry {
    PhonebookRepository repository = PhonebookRepository::contacts;
    std::string name;
    // Newline-separated TYPE<TAB>number records and newline-separated addresses.
    std::string phones, addresses, timestamp;
};

// Compact, bounded multi-repository directory. Strings are packed into one
// buffer because Bluetooth profiles leave little heap for a thousand records.
class Phonebook {
    struct Ref {
        uint16_t name_at, phones_at, addresses_at, timestamp_at, addresses_size;
        uint8_t name_size, phones_size, timestamp_size, repository;
    };
    std::vector<Ref> entries_;
    std::vector<char> text_;
    bool ready_ = false;
  public:
    static constexpr size_t max_entries = 1000;
    static constexpr size_t max_text = 60000;
    void clear();
    bool add(const PhonebookEntry &entry);
    bool add(const std::string &name, const std::string &number);
    size_t size() const { return entries_.size(); }
    size_t size(PhonebookRepository repository) const;
    size_t text_size() const { return text_.size(); }
    PhonebookEntry at(size_t index) const;
    void ready(bool value) { ready_ = value; }
    bool ready() const { return ready_; }
};

// Read-only PBAP 1.1 server used by the car-side adapter. It supports the
// standard download, listing and single-vCard paths while streaming bodies at
// the negotiated MTU instead of constructing a second full phonebook in RAM.
class PbapServer {
    Phonebook &phonebook_;
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
    explicit PbapServer(Phonebook &phonebook) : phonebook_(phonebook) {}
    Bytes request(const Bytes &packet);
    void reset();
};
std::string handle_text(uint64_t value);
} // namespace bridge
