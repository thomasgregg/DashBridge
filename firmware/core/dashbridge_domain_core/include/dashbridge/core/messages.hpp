#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dashbridge::core::messages {

struct Message {
    uint32_t id = 0;
    std::string app, title, subtitle, body, date;
};

enum class ChangeKind : uint8_t {
    reset,
    add,
    update,
    remove,
    history_add,
    local_test,
};

struct Change {
    ChangeKind kind = ChangeKind::reset;
    uint32_t session = 0;
    Message message;
};

struct StoredMessage {
    uint64_t handle = 0;
    Message message;
    bool read = false;
};

class Store {
    uint32_t session_ = 0;
    uint64_t next_handle_ = 1;
    std::vector<StoredMessage> messages_;

  public:
    static constexpr size_t capacity = 32;

    void clear();
    // Only a genuinely new live add returns a handle. History is deliberately
    // silent, while updates and removals never resurrect missing messages.
    uint64_t apply(const Change &change);
    uint32_t session() const { return session_; }
    const std::vector<StoredMessage> &messages() const { return messages_; }
    const StoredMessage *find(uint64_t handle) const;
    bool mark_read(uint64_t handle, bool read);
    bool erase(uint64_t handle);
};

} // namespace dashbridge::core::messages
