#include "dashbridge/core/messages.hpp"
#include "dashbridge/core/text.hpp"
#include <algorithm>

namespace dashbridge::core::messages {

void Store::clear() { messages_.clear(); }

uint64_t Store::apply(const Change &change) {
    const bool local = change.kind == ChangeKind::local_test;
    if (!local && (change.kind == ChangeKind::reset || change.session != session_)) {
        clear();
        session_ = change.session;
    }
    if (change.kind == ChangeKind::reset) return 0;

    const auto existing = local ? messages_.end() :
        std::find_if(messages_.begin(), messages_.end(), [&](const StoredMessage &stored) {
            return stored.message.id == change.message.id;
        });
    if (change.kind == ChangeKind::remove) {
        if (existing != messages_.end()) messages_.erase(existing);
        return 0;
    }
    if (change.message.app.empty()) return 0;

    Message clean = change.message;
    clean.title = core::clean_text(clean.title, 128);
    clean.subtitle = core::clean_text(clean.subtitle, 128);
    clean.body = core::clean_text(clean.body, 768);
    if (existing != messages_.end()) {
        existing->message = std::move(clean);
        return 0;
    }
    if (change.kind != ChangeKind::add && change.kind != ChangeKind::history_add && !local) return 0;
    if (messages_.size() >= capacity) messages_.erase(messages_.begin());

    const uint64_t handle = next_handle_++;
    messages_.push_back({handle, std::move(clean), false});
    return change.kind == ChangeKind::add || local ? handle : 0;
}

const StoredMessage *Store::find(uint64_t handle) const {
    for (const auto &message : messages_)
        if (message.handle == handle) return &message;
    return nullptr;
}

bool Store::mark_read(uint64_t handle, bool read) {
    for (auto &message : messages_) {
        if (message.handle == handle) {
            message.read = read;
            return true;
        }
    }
    return false;
}

bool Store::erase(uint64_t handle) {
    const auto message = std::find_if(messages_.begin(), messages_.end(),
                                      [&](const StoredMessage &stored) {
                                          return stored.handle == handle;
                                      });
    if (message == messages_.end()) return false;
    messages_.erase(message);
    return true;
}

} // namespace dashbridge::core::messages
