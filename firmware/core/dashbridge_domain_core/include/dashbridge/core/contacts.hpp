#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dashbridge::core::contacts {

enum class Repository : uint8_t {
    contacts = 0,
    favorites = 1,
    incoming = 2,
    outgoing = 3,
    missed = 4,
    combined = 5,
};

struct Entry {
    Repository repository = Repository::contacts;
    std::string name;
    // Newline-separated TYPE<TAB>number records and newline-separated addresses.
    std::string phones;
    std::string addresses;
    std::string timestamp;
};

// Compact, bounded multi-repository directory. Strings share one packed buffer
// so the domain can hold a large phonebook without coupling to Bluetooth heaps.
class Store {
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
    bool add(const Entry &entry);
    bool add(const std::string &name, const std::string &number);
    size_t size() const { return entries_.size(); }
    size_t size(Repository repository) const;
    size_t text_size() const { return text_.size(); }
    Entry at(size_t index) const;
    void ready(bool value) { ready_ = value; }
    bool ready() const { return ready_; }
};

struct TransferAck {
    uint32_t session = 0;
    size_t count = 0;
    bool accepted = false;
};

enum class ReceiveResult { ignored, accepted, completed, rejected };

// Atomic receiver for an ordered board-to-board directory snapshot. Traffic
// from stale sessions is ignored and therefore cannot poison the active sync.
class TransferReceiver {
    Store &store_;
    Store staged_;
    uint32_t active_session_ = 0;
    size_t expected_sequence_ = 0;
    bool invalid_ = false;
    std::optional<TransferAck> pending_ack_;

  public:
    explicit TransferReceiver(Store &store) : store_(store) {}
    ReceiveResult reset(uint32_t session);
    ReceiveResult entry(uint32_t session, size_t sequence, const Entry &entry);
    ReceiveResult done(uint32_t session, size_t count);
    const std::optional<TransferAck> &pending_ack() const { return pending_ack_; }
    void ack_sent() { pending_ack_.reset(); }
    uint32_t active_session() const { return active_session_; }
    size_t received() const { return expected_sequence_; }
};

enum class SendStep { idle, reset, wait_source, entry, done, wait_ack, complete };
enum class AckResult { ignored, complete, retry };

// Transport-independent sender cursor. The adapter chooses how each step is
// encoded and calls sent() only after its transport accepted that frame.
class TransferSender {
    uint32_t session_ = 0;
    size_t next_ = 0;
    SendStep step_ = SendStep::idle;
    bool source_ready_ = false;

  public:
    bool start(uint32_t session, bool source_ready);
    void source_ready();
    void retry();
    SendStep step(size_t total) const;
    size_t next() const { return next_; }
    uint32_t session() const { return session_; }
    void sent(size_t total);
    AckResult accept(const TransferAck &ack, size_t total);
};

} // namespace dashbridge::core::contacts
