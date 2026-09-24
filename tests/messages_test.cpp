#include "dashbridge/core/messages.hpp"
#include <cassert>
#include <iostream>
#include <string>

namespace messages = dashbridge::core::messages;

static messages::Message example(uint32_t id = 1) {
    return {id, "net.whatsapp.WhatsApp", "Alice", "Team", "Hello", "20260924T120000"};
}

int main() {
    messages::Store store;
    auto live = store.apply({messages::ChangeKind::add, 7, example()});
    assert(live && store.session() == 7 && store.messages().size() == 1);

    // Duplicate adds update in place without a second audible event or handle.
    auto changed = example();
    changed.body = "Edited";
    assert(!store.apply({messages::ChangeKind::add, 7, changed}));
    assert(store.messages().size() == 1 && store.find(live)->message.body == "Edited");

    assert(store.mark_read(live, true) && store.find(live)->read);
    changed.body = "Edited again";
    store.apply({messages::ChangeKind::update, 7, changed});
    assert(store.find(live)->read); // Content updates preserve Tesla's local read state.

    // Missing updates/removals never resurrect a notification.
    assert(!store.apply({messages::ChangeKind::update, 7, example(2)}));
    assert(!store.apply({messages::ChangeKind::remove, 7, example(2)}));
    assert(store.messages().size() == 1);

    // Notification Center history is queryable by MAP but never produces NewMessage.
    assert(!store.apply({messages::ChangeKind::history_add, 7, example(3)}));
    assert(store.messages().size() == 2);

    // A local test neither resets the active session nor collides with a real
    // ANCS identifier, even when both source IDs happen to be identical.
    assert(store.apply({messages::ChangeKind::local_test, 0, example()}));
    assert(store.session() == 7);
    assert(store.messages().size() == 3);

    // A new phone session and an explicit reset both invalidate the projection.
    assert(store.apply({messages::ChangeKind::add, 8, example(5)}));
    assert(store.messages().size() == 1 && store.session() == 8);
    store.apply({messages::ChangeKind::reset, 9, {}});
    assert(store.messages().empty() && store.session() == 9);

    auto invalid = example(6);
    invalid.app.clear();
    assert(!store.apply({messages::ChangeKind::add, 9, invalid}) && store.messages().empty());

    auto oversized = example(7);
    oversized.title.assign(200, 't');
    oversized.subtitle.assign(200, 's');
    oversized.body = std::string(767, 'x') + "👋";
    const auto bounded = store.apply({messages::ChangeKind::add, 9, oversized});
    assert(bounded);
    const auto *saved = store.find(bounded);
    assert(saved && saved->message.title.size() == 128 && saved->message.subtitle.size() == 128);
    assert(saved->message.body == std::string(767, 'x')); // Never split UTF-8.

    store.clear();
    for (size_t i = 0; i < messages::Store::capacity + 8; ++i)
        assert(store.apply({messages::ChangeKind::add, 9, example(uint32_t(100 + i))}));
    assert(store.messages().size() == messages::Store::capacity);
    assert(store.messages().front().message.id == 108); // Oldest entries are evicted first.

    const auto handle = store.messages().front().handle;
    assert(store.erase(handle) && !store.find(handle) && !store.erase(handle));
    assert(!store.mark_read(UINT64_MAX, true));

    std::cout << "Message state, history, session, privacy bounds and capacity tests passed\n";
}
