#include "app_internal.hpp"
#include "dashbridge/adapters/features.hpp"
#include "dashbridge/core/connections.hpp"
#include "dashbridge/platform/console_commands.hpp"
#include "dashbridge/platform/esp_runtime.hpp"
#include "dashbridge/ports/runtime_services.hpp"
#include "dashbridge/transport/dashlink_transport.hpp"
#include "sdkconfig.h"
namespace runtime {
using namespace dashbridge::ports;
namespace setup = dashbridge::core::setup;
namespace dashlink = dashbridge::protocols::dashlink_v2;
namespace {

dashbridge::platform::EspRuntime platform_runtime;
dashbridge::transport::ControlQueue outgoing;

class RuntimeSetupActions final : public setup::Actions {
  public:
    int64_t now_ms() const override { return platform_runtime.now_ms(); }
    void lock() override { platform_runtime.lock(); }
    void unlock() override { platform_runtime.unlock(); }
    std::string recent_application_name(std::string_view id) const override {
#if CONFIG_BRIDGE_PHONE
        return phone_setup_recent_application_name(id);
#else
        (void)id;
        return {};
#endif
    }
    bool persist_policy(const setup::Bytes &bytes) override {
#if CONFIG_BRIDGE_PHONE
        return phone_setup_save_policy(bytes);
#else
        (void)bytes;
        return false;
#endif
    }
    bool start_test_notification() override {
#if CONFIG_BRIDGE_PHONE
        return phone_setup_test_notification();
#else
        return false;
#endif
    }
};

RuntimeSetupActions setup_actions;
setup::Controller setup_state(setup_actions);
dashbridge::core::connections::Coordinator classic_connections;

setup::Peer setup_peer(Peer peer) {
    return peer == Peer::phone ? setup::Peer::phone : setup::Peer::tesla;
}

class AppServices final : public dashbridge::ports::RuntimeServices {
  public:
    int64_t now_ms() const override { return platform_runtime.now_ms(); }
    uint32_t random_token() const override { return platform_runtime.random_token(); }
    bool classic_bond_known(const uint8_t *address) const override {
        return platform_runtime.classic_bond_known(address);
    }
    void classic_base_link_changed(bool connected) override {
        classic_connections.base_link_changed(connected);
    }
    bool try_begin_classic_profile(dashbridge::core::connections::Profile profile) override {
        return classic_connections.try_begin(profile);
    }
    void finish_classic_profile(dashbridge::core::connections::Profile profile) override {
        classic_connections.finish(profile);
    }
    void lock() override { platform_runtime.lock(); }
    void unlock() override { platform_runtime.unlock(); }
    bool send_to_phone(const dashlink::Packet &packet) override { return send(packet, false); }
    bool send_to_car(const dashlink::Packet &packet) override { return send(packet, true); }
    bool pairing_allowed(Peer peer) const override { return setup_state.pairing_allowed(setup_peer(peer)); }
    void paired(Peer peer) override {
#if CONFIG_BRIDGE_CALL_RELAY && CONFIG_BRIDGE_PHONE
        if (peer == Peer::phone && (!phone_notifications_ready() || !relay_calls_ready())) return;
#endif
        setup_state.paired(setup_peer(peer));
    }
    setup::Controller &setup_controller() override { return setup_state; }
    void emit_setup_json(const std::string &json) override {
        platform_runtime.write_console("@DB " + json + "\r\n");
    }

  private:
    bool send(const dashlink::Packet &packet, bool to_car) {
        Guard guard;
        (void)to_car;
        const bool accepted = outgoing.push(packet);
        if (!accepted) platform_runtime.warn("dashlink", "Typed control queue full or packet invalid");
        return accepted;
    }
};

AppServices app_services;

} // namespace

static void receive_packet(const dashlink::Packet &packet, bool for_phone) {
#if CONFIG_BRIDGE_CONTACT_SYNC
    if (std::holds_alternative<dashlink::ContactReset>(packet) ||
        std::holds_alternative<dashlink::ContactEntry>(packet) ||
        std::holds_alternative<dashlink::ContactDone>(packet) ||
        std::holds_alternative<dashlink::ContactAck>(packet)) {
        contacts_receive(packet);
        return;
    }
#endif
#if CONFIG_BRIDGE_CALL_RELAY
    if (const auto *call = std::get_if<dashlink::Call>(&packet)) {
        relay_receive(*call);
        return;
    }
#endif
#if CONFIG_BRIDGE_MUSIC_RELAY
    if (std::holds_alternative<dashlink::MusicState>(packet) ||
        std::holds_alternative<dashlink::MusicCommand>(packet)) {
        music_control_receive(packet);
        return;
    }
#endif
    if (const auto *heartbeat = std::get_if<dashlink::Heartbeat>(&packet)) {
#if CONFIG_BRIDGE_PHONE
        if (for_phone) phone_receive(*heartbeat);
#else
        if (!for_phone) car_receive(*heartbeat);
#endif
        return;
    }
#if CONFIG_BRIDGE_CAR
    if (!for_phone)
        if (const auto *notification = std::get_if<dashlink::Notification>(&packet))
            car_receive(*notification);
#else
    (void)for_phone;
#endif
}
static void open_pairing(Peer peer) {
    setup_state.open_pairing(setup_peer(peer));
    platform_runtime.info("setup", "%s pairing open for 120 seconds", peer == Peer::phone ? "iPhone" : "Tesla");
}
static void open_pairing() {
#if CONFIG_BRIDGE_PHONE
    open_pairing(Peer::phone);
#else
    open_pairing(Peer::car);
#endif
}
static void status() {
    platform_runtime.info("status", "Firmware: %s", platform_runtime.firmware_version());
#if CONFIG_BRIDGE_CALL_RELAY
    relay_status();
#endif
#if CONFIG_BRIDGE_PHONE
    platform_runtime.info("status", "iPhone notifications: %s; pairing: %s", phone_notifications_ready() ? "ready" : "not ready",
             pairing_allowed(Peer::phone) ? "open" : "closed");
    phone_status();
#else
    platform_runtime.info("status", "Tesla notifications: %s; pairing: %s", car_notifications_ready() ? "ready" : "not ready",
             pairing_allowed(Peer::car) ? "open" : "closed");
#endif
    platform_runtime.log_memory();
}
static void setup_status() {
    const char *role =
#if CONFIG_BRIDGE_PHONE
        "phone";
#else
        "car";
#endif
    bool board_link =
#if CONFIG_BRIDGE_PHONE
        phone_board_link_ready();
#else
        car_board_link_ready();
#endif
    bool phone_ready =
#if CONFIG_BRIDGE_PHONE
        phone_notifications_ready();
#else
        car_phone_notification_ready();
#endif
    bool phone_bluetooth =
#if CONFIG_BRIDGE_PHONE
        phone_bluetooth_ready();
#else
        car_phone_bluetooth_ready();
#endif
    bool car_ready =
#if CONFIG_BRIDGE_CAR
        car_notifications_ready();
#else
        phone_car_message_ready();
#endif
    bool car_transport =
#if CONFIG_BRIDGE_CAR
        car_message_transport_ready();
#else
        phone_car_transport_ready();
#endif
    bool car_sync =
#if CONFIG_BRIDGE_CAR
        car_message_sync_ready();
#else
        phone_car_sync_ready();
#endif
    bool calls_ready = false;
    bool phone_calls_ready = false;
    bool car_calls_ready = false;
#if CONFIG_BRIDGE_CALL_RELAY
    calls_ready = relay_calls_ready();
    #if CONFIG_BRIDGE_PHONE
    phone_calls_ready = calls_ready;
    car_calls_ready = phone_car_call_ready();
    #elif CONFIG_BRIDGE_CAR
    car_calls_ready = calls_ready;
    phone_calls_ready = car_phone_call_ready();
    #endif
#endif
    setup_emit("{\"type\":\"status\",\"role\":" + setup_escape(role) +
               ",\"version\":" + setup_escape(platform_runtime.firmware_version()) +
               ",\"boardLink\":" + (board_link ? "true" : "false") +
               ",\"phoneBluetooth\":" + (phone_bluetooth ? "true" : "false") +
               ",\"phoneNotifications\":" + (phone_ready ? "true" : "false") +
               ",\"carTransport\":" + (car_transport ? "true" : "false") +
               ",\"carSync\":" + (car_sync ? "true" : "false") +
               ",\"carMessages\":" + (car_ready ? "true" : "false") +
               ",\"callProfile\":" + (calls_ready ? "true" : "false") +
               ",\"phoneCalls\":" + (phone_calls_ready ? "true" : "false") +
               ",\"carCalls\":" + (car_calls_ready ? "true" : "false") + "}");
}
static void setup_result(bool ok, const char *message) {
    setup_emit("{\"type\":\"result\",\"ok\":" + std::string(ok ? "true" : "false") +
               ",\"message\":" + setup_escape(message) + "}");
}
static void setup_command(const std::string &line) {
    if (line == "db status") { setup_status(); return; }
    if (line == "db pair") { open_pairing(); setup_result(true, "Pairing opened for two minutes."); return; }
#if CONFIG_BRIDGE_PHONE
    if (line == "db apps") { phone_setup_apps(); return; }
    if (line == "db discover") {
        bool ok = phone_setup_discover();
        setup_result(ok, ok ? "Discovery is open for one minute. Send a new notification from that app."
                            : "Connect the iPhone and allow notification sharing first.");
        return;
    }
    if (line.rfind("db allow ", 0) == 0) {
        auto value = line.substr(9);
        auto sep = value.rfind(' ');
        bool ok = false;
        if (sep != std::string::npos && sep + 2 == value.size() && value[sep + 1] >= '0' && value[sep + 1] <= '2')
            ok = phone_setup_allow(value.substr(0, sep), setup::Preview(value[sep + 1] - '0'));
        setup_result(ok, ok ? "App choice saved on Board A." : "Could not save this app. Discover it first and try again.");
        if (ok) phone_setup_apps();
        return;
    }
    if (line.rfind("db deny ", 0) == 0) {
        bool ok = phone_setup_deny(line.substr(8));
        setup_result(ok, ok ? "App removed from allowed list." : "Could not remove this app.");
        if (ok) phone_setup_apps();
        return;
    }
#endif
#if CONFIG_BRIDGE_CAR
    if (line == "db test") {
        bool ok = car_notifications_ready();
        if (ok) ok = car_test();
        setup_result(ok, ok ? "Test message sent. Check the Tesla screen." :
                     "Could not send a test message. Check the Tesla connection and try again.");
        return;
    }
#endif
    setup_result(false, "This action is not available on this board.");
}
static void console_command(Command command) {
    if (command == Command::none) return;
    if (command == Command::status) { status(); return; }
#if CONFIG_BRIDGE_CALL_RELAY
    using Mode = calls::AudioTestMode;
    if (command == Command::audio_off) { relay_audio_test_stop(); return; }
    if (command == Command::audio_phone_tone) { relay_audio_test(true, Mode::tone); return; }
    if (command == Command::audio_phone_loopback) { relay_audio_test(true, Mode::loopback); return; }
    if (command == Command::audio_car_tone) { relay_audio_test(false, Mode::tone); return; }
    if (command == Command::audio_car_loopback) { relay_audio_test(false, Mode::loopback); return; }
#endif
#if CONFIG_BRIDGE_PHONE
    if (command == Command::pair_phone) { open_pairing(Peer::phone); return; }
#endif
#if CONFIG_BRIDGE_CAR
    if (command == Command::pair_car) { open_pairing(Peer::car); return; }
    if (command == Command::test) { car_test(); return; }
#endif
#if CONFIG_BRIDGE_CALL_RELAY
    platform_runtime.info("setup", "Audio tests (active call, 60s): audio phone tone, audio phone loopback, audio car tone, audio car loopback, audio off");
#endif
#if CONFIG_BRIDGE_CAR
    platform_runtime.info("setup", "USB commands: test, pair car, status, help");
#else
    platform_runtime.info("setup", "USB commands: pair phone, status, help. Send test on the Tesla receiver.");
#endif
}
} // namespace runtime
extern "C" void app_main() {
    using namespace runtime;
    platform_runtime.initialize();
    dashbridge::ports::install_runtime_services(app_services);
    {
    Guard startup_guard;
#if CONFIG_BRIDGE_PHONE
    if (!platform_runtime.has_ble_bond()
#if CONFIG_BRIDGE_CALL_RELAY
        || !platform_runtime.has_classic_bond()
#endif
    ) open_pairing(Peer::phone);
    phone_start();
    setup_ble_start();
#else
    if (!platform_runtime.has_classic_bond()) open_pairing(Peer::car);
    car_start();
#endif
#if CONFIG_BRIDGE_CALL_RELAY
    relay_start();
#endif
    }
    dashlink::Decoder decoder;
    uint8_t buf[256];
    ConsoleCommands console;
    SetupLines setup_lines;
    console_command(Command::help);
    uint8_t console_buf[64];
    int64_t down = 0;
    bool pressed = false;
    int64_t last_status = 0;
    for (;;) {
        int n = platform_runtime.read_control(buf, sizeof buf, 20);
        int console_n = platform_runtime.read_console(console_buf, sizeof console_buf);
        {
            Guard g;
            for (int i = 0; i < console_n; ++i) {
                console_command(console.feed(console_buf[i]));
                auto command = setup_lines.feed(console_buf[i]);
                if (!command.empty()) setup_command(command);
            }
            if (n > 0)
                decoder.feed(buf, n, [](dashlink::Packet packet) {
#if CONFIG_BRIDGE_PHONE
                    receive_packet(packet, true);
#else
                    receive_packet(packet, false);
#endif
                });
            bool low = platform_runtime.button_pressed();
            if (low && !pressed) {
                pressed = true;
                down = now();
            }
            if (!low && pressed) {
                pressed = false;
                auto held = now() - down;
                if (held >= 8000) {
                    platform_runtime.warn("setup", "Physical reset: erase this adapter's pairing and restart");
                    platform_runtime.erase_pairing_and_restart();
                } else if (held >= 2000)
                    open_pairing();
#if CONFIG_BRIDGE_CAR
                else if (held >= 50)
                    car_test();
#endif
            }
#if CONFIG_BRIDGE_CAR
            car_poll();
#endif
#if CONFIG_BRIDGE_PHONE
            phone_poll();
            setup_refresh_status();
#endif
#if CONFIG_BRIDGE_CALL_RELAY
            relay_poll();
#endif
            if (now() - last_status >= 30000) {
                last_status = now();
                status();
            }
        }
        dashlink::Bytes frame;
        {
            Guard g;
            outgoing.pop(frame);
        }
        if (!frame.empty() && !platform_runtime.write_control(frame.data(), frame.size()))
            platform_runtime.warn("dashlink", "UART control write failed");
    }
}
