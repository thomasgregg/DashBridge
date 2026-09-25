#include "dashbridge/core/setup.hpp"
#include "dashbridge/protocols/setup_gatt_v1.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace setup = dashbridge::core::setup;
namespace protocol = dashbridge::protocols::setup_gatt_v1;

namespace {
int failures = 0;

class FakeActions final : public setup::Actions {
  public:
    int64_t time = 0;
    int lock_depth = 0;
    std::string recent_name;
    setup::Bytes persisted;
    bool persistence_succeeds = true;
    bool test_succeeds = true;

    int64_t now_ms() const override { return time; }
    void lock() override { ++lock_depth; }
    void unlock() override { --lock_depth; }
    std::string recent_application_name(std::string_view) const override { return recent_name; }
    bool persist_policy(const setup::Bytes &bytes) override {
        if (!persistence_succeeds) return false;
        persisted = bytes;
        return true;
    }
    bool start_test_notification() override { return test_succeeds; }
};

void expect(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL setup: %s\n", message);
        ++failures;
    }
}

protocol::DecodedCommand decode(const std::vector<uint8_t> &bytes) {
    return protocol::decode_command(bytes.data(), bytes.size());
}
} // namespace

int main() {
    setup::Status status;
    status.phone_bluetooth = true;
    status.notifications = true;
    status.phone_calls = true;
    status.internal_link = true;
    status.tesla_messages = true;
    status.tesla_transport = true;
    status.tesla_sync = true;
    status.tesla_calls = true;
    status.phone_pairing_open = true;
    expect(protocol::encode_status(status) == std::array<uint8_t, 3>{1, 0xff, 0x01},
           "status v1 preserves every released bit");
    expect(protocol::encode_status({}) == std::array<uint8_t, 3>{1, 0, 0},
           "empty status has only its version byte");

    expect(protocol::encode_policy({}).empty(), "empty policy is zero bytes");
    expect(protocol::encode_policy({"com.apple.MobileSMS", "net.whatsapp.WhatsApp"}) ==
               "com.apple.MobileSMS\nnet.whatsapp.WhatsApp\n",
           "policy remains newline-delimited with a trailing newline");

    auto command = decode({1, 'n', 'e', 't', '.', 'a', 'p', 'p'});
    expect(command.result == protocol::DecodeResult::decoded,
           "allow command decodes");
    expect(command.command.kind == setup::CommandKind::allow_application &&
               command.command.application_id == "net.app",
           "allow command retains its application identifier");

    command = decode({2, 'n', 'e', 't', '.', 'a', 'p', 'p'});
    expect(command.result == protocol::DecodeResult::decoded &&
               command.command.kind == setup::CommandKind::deny_application,
           "deny command decodes");
    command = decode({3});
    expect(command.result == protocol::DecodeResult::decoded &&
               command.command.kind == setup::CommandKind::open_phone_pairing,
           "pairing command decodes");
    command = decode({4});
    expect(command.result == protocol::DecodeResult::decoded &&
               command.command.kind == setup::CommandKind::test_notification,
           "test-notification command decodes");

    expect(protocol::decode_command(nullptr, 1).result == protocol::DecodeResult::invalid_pdu,
           "null command data is rejected");
    expect(protocol::decode_command(nullptr, 0).result == protocol::DecodeResult::invalid_pdu,
           "empty command is rejected");
    expect(decode({1}).result == protocol::DecodeResult::invalid_pdu,
           "allow requires an application identifier");
    expect(decode({2, 'n', 'e', 't', '/', 'a', 'p', 'p'}).result ==
               protocol::DecodeResult::invalid_pdu,
           "invalid application identifier characters are rejected");
    expect(decode({3, 0}).result == protocol::DecodeResult::invalid_pdu,
           "pairing command rejects trailing bytes");
    expect(decode({4, 0}).result == protocol::DecodeResult::invalid_pdu,
           "test command rejects trailing bytes");
    expect(decode({99}).result == protocol::DecodeResult::unsupported,
           "unknown single-byte command remains unsupported rather than malformed");
    expect(decode({99, 0}).result == protocol::DecodeResult::invalid_pdu,
           "unknown multi-byte command remains malformed");

    std::vector<uint8_t> maximum(96, 'a');
    maximum[0] = 1;
    expect(decode(maximum).result == protocol::DecodeResult::decoded,
           "maximum-size command is accepted");
    maximum.push_back('a');
    expect(decode(maximum).result == protocol::DecodeResult::invalid_pdu,
           "oversize command is rejected");

    expect(setup::valid_application_id("com.example_app-name"),
           "portable core accepts released application identifier characters");
    expect(!setup::valid_application_id(""), "portable core rejects an empty identifier");
    expect(!setup::valid_application_id("bad id"),
           "portable core rejects whitespace in an identifier");

    FakeActions actions;
    setup::Controller controller(actions);
    setup::Status projected;
    projected.phone_bluetooth = true;
    projected.notifications = true;
    projected.phone_pairing_open = true; // Pairing is controller-owned, not caller-owned.
    controller.update_status(projected);
    expect(controller.status().phone_bluetooth && controller.status().notifications,
           "controller owns the current setup status projection");
    expect(!controller.status().phone_pairing_open,
           "status updates cannot forge the controller-owned pairing bit");

    actions.time = 1000;
    controller.open_pairing(setup::Peer::phone);
    controller.open_pairing(setup::Peer::tesla);
    controller.paired(setup::Peer::phone);
    expect(!controller.pairing_allowed(setup::Peer::phone),
           "pairing one transport closes only that transport");
    expect(controller.pairing_allowed(setup::Peer::tesla),
           "the other pairing window remains open");
    actions.time = 120999;
    expect(controller.pairing_allowed(setup::Peer::tesla),
           "pairing window remains open through its released duration");
    actions.time = 121000;
    expect(!controller.pairing_allowed(setup::Peer::tesla),
           "pairing window closes at exactly 120 seconds");

    actions.recent_name = "Example Chat";
    expect(controller.execute({setup::CommandKind::allow_application, "com.example.Chat"}) ==
               setup::CommandResult::applied,
           "controller applies and persists an allow command");
    const setup::Bytes released_policy = {
        1, 1, 16, 'c', 'o', 'm', '.', 'e', 'x', 'a', 'm', 'p', 'l', 'e', '.', 'C', 'h', 'a', 't',
        12, 'E', 'x', 'a', 'm', 'p', 'l', 'e', ' ', 'C', 'h', 'a', 't', 0,
    };
    expect(actions.persisted == released_policy,
           "portable policy serialization stays byte-compatible with installed boards");
    expect(controller.allowed_application_ids() == std::vector<std::string>{"com.example.Chat"},
           "controller is the allow-list source of truth");

    actions.persistence_succeeds = false;
    expect(!controller.allow_application("com.example.Other", setup::Preview::sender),
           "failed persistence rejects a policy mutation");
    expect(controller.allowed_application_ids() == std::vector<std::string>{"com.example.Chat"},
           "failed persistence rolls policy back atomically");
    actions.persistence_succeeds = true;
    expect(controller.deny_application("com.example.Chat") &&
               controller.allowed_application_ids().empty(),
           "deny removes and persists an allowed application");

    expect(controller.restore_policy(released_policy),
           "controller restores the released policy format");
    setup::ApplicationRule restored;
    expect(controller.find_application_rule("com.example.Chat", restored) &&
               restored.name == "Example Chat" && restored.preview == setup::Preview::full,
           "restored policy keeps its display name and preview mode");
    expect(!controller.restore_policy({1, 1, 5, 'b', 'a', 'd'}),
           "truncated saved policy is rejected without replacing current state");

    expect(controller.execute({setup::CommandKind::test_notification, {}}) ==
               setup::CommandResult::applied,
           "test command delegates its single runtime effect");
    actions.test_succeeds = false;
    expect(controller.execute({setup::CommandKind::test_notification, {}}) ==
               setup::CommandResult::rejected,
           "failed runtime test effect is reported to the client");
    expect(actions.lock_depth == 0, "every controller operation releases its critical section");

    if (failures) return 1;
    std::puts("Setup core and GATT v1 compatibility tests passed");
    return 0;
}
