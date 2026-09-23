#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "nvs_flash.h"
#include "runtime.hpp"
#include "sdkconfig.h"
#include "console_commands.hpp"
#include "call_relay.hpp"
#include <deque>
#include <cstdio>
namespace runtime {
SemaphoreHandle_t mutex;
static PairingWindows pairing;
std::string setup_escape(const std::string &value) {
    std::string out = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c < 32) {
            char code[7];
            std::snprintf(code, sizeof code, "\\u%04x", unsigned(c));
            out += code;
        } else out += char(c);
    }
    return out + '"';
}
void setup_emit(const std::string &json) {
    auto line = "@DB " + json + "\r\n";
    uart_write_bytes(UART_NUM_0, line.data(), line.size());
}
#if CONFIG_BRIDGE_SINGLE
static LocalBridge local;
bool send_to_car(const bridge::WireMessage &m) {
    Guard g;
    const bool accepted = local.send_to_car(m);
    if (!accepted) ESP_LOGW("bridge", "Local queue full; notification dropped");
    return accepted;
}
bool send_to_phone(const bridge::WireMessage &m) {
    Guard g;
    return local.send_to_phone(m);
}
#else
static std::deque<bridge::Bytes> outgoing, call_outgoing;
static bool send_wire(const bridge::WireMessage &m) {
    Guard g;
    auto &queue = m.op == bridge::Op::call ? call_outgoing : outgoing;
    if (queue.size() < 32) { queue.push_back(bridge::encode(m)); return true; }
    ESP_LOGW("wire", "Queue full; notification dropped");
    return false;
}
bool send_to_car(const bridge::WireMessage &m) { return send_wire(m); }
bool send_to_phone(const bridge::WireMessage &m) { return send_wire(m); }
#endif
bool pairing_allowed(Peer peer) { return pairing.allowed(peer, now()); }
void paired(Peer peer) {
#if CONFIG_BRIDGE_CALL_RELAY && CONFIG_BRIDGE_PHONE
    // BLE and Classic must both pair before closing the same 120-second window.
    if (peer == Peer::phone && (!phone_notifications_ready() || !relay_calls_ready())) return;
#endif
    pairing.paired(peer);
}
static void open_pairing(Peer peer) {
    pairing.open(peer, now());
    ESP_LOGI("setup", "%s pairing open for 120 seconds", peer == Peer::phone ? "iPhone" : "Tesla");
}
void setup_open_pairing(Peer peer) { open_pairing(peer); }
static void open_pairing() {
#if CONFIG_BRIDGE_PHONE || CONFIG_BRIDGE_SINGLE
    open_pairing(Peer::phone);
#endif
#if CONFIG_BRIDGE_CAR || CONFIG_BRIDGE_SINGLE
    open_pairing(Peer::car);
#endif
}
static void status() {
    ESP_LOGI("status", "Firmware: %s", esp_app_get_description()->version);
#if CONFIG_BRIDGE_CALL_RELAY
    relay_status();
#endif
#if CONFIG_BRIDGE_PHONE || CONFIG_BRIDGE_SINGLE
    ESP_LOGI("status", "iPhone notifications: %s; pairing: %s", phone_notifications_ready() ? "ready" : "not ready",
             pairing_allowed(Peer::phone) ? "open" : "closed");
    phone_status();
#endif
#if CONFIG_BRIDGE_CAR || CONFIG_BRIDGE_SINGLE
    ESP_LOGI("status", "Tesla notifications: %s; pairing: %s", car_notifications_ready() ? "ready" : "not ready",
             pairing_allowed(Peer::car) ? "open" : "closed");
#endif
    ESP_LOGI("status", "Internal RAM: free=%u minimum=%u largest=%u bytes",
             unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
#if CONFIG_BRIDGE_SINGLE
    ESP_LOGI("status", "Local messages queued: %u", unsigned(local.queued()));
#endif
}
static void setup_status() {
    const char *role =
#if CONFIG_BRIDGE_PHONE
        "phone";
#elif CONFIG_BRIDGE_CAR
        "car";
#else
        "single";
#endif
    bool board_link =
#if CONFIG_BRIDGE_PHONE
        phone_board_link_ready();
#elif CONFIG_BRIDGE_CAR
        car_board_link_ready();
#else
        true;
#endif
    bool phone_ready =
#if CONFIG_BRIDGE_PHONE || CONFIG_BRIDGE_SINGLE
        phone_notifications_ready();
#else
        car_phone_notification_ready();
#endif
    bool phone_bluetooth =
#if CONFIG_BRIDGE_PHONE || CONFIG_BRIDGE_SINGLE
        phone_bluetooth_ready();
#else
        car_phone_bluetooth_ready();
#endif
    bool car_ready =
#if CONFIG_BRIDGE_CAR || CONFIG_BRIDGE_SINGLE
        car_notifications_ready();
#else
        phone_car_message_ready();
#endif
    bool car_transport =
#if CONFIG_BRIDGE_CAR || CONFIG_BRIDGE_SINGLE
        car_message_transport_ready();
#else
        phone_car_transport_ready();
#endif
    bool car_sync =
#if CONFIG_BRIDGE_CAR || CONFIG_BRIDGE_SINGLE
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
               ",\"version\":" + setup_escape(esp_app_get_description()->version) +
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
#if CONFIG_BRIDGE_PHONE || CONFIG_BRIDGE_SINGLE
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
            ok = phone_setup_allow(value.substr(0, sep), bridge::Preview(value[sep + 1] - '0'));
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
#if CONFIG_BRIDGE_CAR || CONFIG_BRIDGE_SINGLE
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
    if (command == Command::pair) { open_pairing(); return; }
    if (command == Command::status) { status(); return; }
#if CONFIG_BRIDGE_CALL_RELAY
    using Mode = calls::AudioTestMode;
    if (command == Command::audio_off) { relay_audio_test_stop(); return; }
    if (command == Command::audio_phone_tone) { relay_audio_test(true, Mode::tone); return; }
    if (command == Command::audio_phone_loopback) { relay_audio_test(true, Mode::loopback); return; }
    if (command == Command::audio_car_tone) { relay_audio_test(false, Mode::tone); return; }
    if (command == Command::audio_car_loopback) { relay_audio_test(false, Mode::loopback); return; }
#endif
#if CONFIG_BRIDGE_PHONE || CONFIG_BRIDGE_SINGLE
    if (command == Command::pair_phone) { open_pairing(Peer::phone); return; }
#endif
#if CONFIG_BRIDGE_CAR || CONFIG_BRIDGE_SINGLE
    if (command == Command::pair_car) { open_pairing(Peer::car); return; }
    if (command == Command::test) { car_test(); return; }
#endif
#if CONFIG_BRIDGE_CALL_RELAY
    ESP_LOGI("setup", "Audio tests (active call, 60s): audio phone tone, audio phone loopback, audio car tone, audio car loopback, audio off");
#endif
#if CONFIG_BRIDGE_SINGLE
    ESP_LOGI("setup", "USB commands: test, pair phone, pair car, pair (both), status, help");
#elif CONFIG_BRIDGE_CAR
    ESP_LOGI("setup", "USB commands: test, pair car, pair, status, help");
#else
    ESP_LOGI("setup", "USB commands: pair phone, pair, status, help. Send test on the Tesla receiver.");
#endif
}
} // namespace runtime
extern "C" void app_main() {
    using namespace runtime;
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        e = nvs_flash_init();
    }
    ESP_ERROR_CHECK(e);
    mutex = xSemaphoreCreateRecursiveMutex();
    configASSERT(mutex);
    uart_config_t u = {};
    u.baud_rate = 115200;
    u.data_bits = UART_DATA_8_BITS;
    u.parity = UART_PARITY_DISABLE;
    u.stop_bits = UART_STOP_BITS_1;
    u.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    u.source_clk = UART_SCLK_DEFAULT;
#if !CONFIG_BRIDGE_SINGLE
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_2, &u));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, 17, 16, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, 4096, 4096, 0, nullptr, 0));
#endif
    // USB-to-UART console on GPIO1/GPIO3; independent of the board link above.
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 512, 0, 0, nullptr, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &u));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_0, 1, 3, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    gpio_config_t b = {};
    b.pin_bit_mask = 1ULL << GPIO_NUM_0;
    b.mode = GPIO_MODE_INPUT;
    b.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&b));
#if CONFIG_BRIDGE_CAR
    // Board B exposes only Classic HFP and MAP, so release unused BLE memory.
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));
#endif
    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&cfg));
#if CONFIG_BRIDGE_CAR
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    ESP_LOGI("bridge", "Board B controller mode: Classic only (HFP + MAP)");
#else
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BTDM));
#endif
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    {
    Guard startup_guard;
#if CONFIG_BRIDGE_SINGLE
    ESP_LOGI("bridge", "DashBridge single-board prototype: iPhone BLE + Tesla Classic. No call/audio relay.");
#endif
#if CONFIG_BRIDGE_PHONE || CONFIG_BRIDGE_SINGLE
    if (esp_ble_get_bond_device_num() == 0
#if CONFIG_BRIDGE_CALL_RELAY
        || esp_bt_gap_get_bond_device_num() == 0
#endif
    ) open_pairing(Peer::phone);
    phone_start();
#endif
#if CONFIG_BRIDGE_CAR || CONFIG_BRIDGE_SINGLE
    if (esp_bt_gap_get_bond_device_num() == 0) open_pairing(Peer::car);
    car_start();
#endif
#if CONFIG_BRIDGE_CALL_RELAY
    relay_start();
#endif
    }
#if !CONFIG_BRIDGE_SINGLE
    bridge::WireDecoder decoder;
    uint8_t buf[256];
#endif
    ConsoleCommands console;
    SetupLines setup_lines;
    console_command(Command::help);
    uint8_t console_buf[64];
    int64_t down = 0;
    bool pressed = false;
    int64_t last_status = 0;
    for (;;) {
#if CONFIG_BRIDGE_SINGLE
        vTaskDelay(pdMS_TO_TICKS(20));
#else
        int n = uart_read_bytes(UART_NUM_2, buf, sizeof buf, pdMS_TO_TICKS(20));
#endif
        int console_n = uart_read_bytes(UART_NUM_0, console_buf, sizeof console_buf, 0);
        {
            Guard g;
            for (int i = 0; i < console_n; ++i) {
                console_command(console.feed(console_buf[i]));
                auto command = setup_lines.feed(console_buf[i]);
                if (!command.empty()) setup_command(command);
            }
        #if !CONFIG_BRIDGE_SINGLE
            if (n > 0)
                decoder.feed(buf, n, [](const bridge::WireMessage &m) {
#if CONFIG_BRIDGE_CONTACT_SYNC
                    if (bridge::contact_op(m.op)) { contacts_receive(m); return; }
#endif
#if CONFIG_BRIDGE_CALL_RELAY
                    if (m.op == bridge::Op::call) { relay_receive(m); return; }
#if CONFIG_BRIDGE_MUSIC_RELAY
                    if (m.op == bridge::Op::media_state || m.op == bridge::Op::media_command) {
                        music_control_receive(m); return;
                    }
#endif
#endif
#if CONFIG_BRIDGE_PHONE
                    phone_receive(m);
#else
                car_receive(m);
#endif
                });
        #endif
            bool low = gpio_get_level(GPIO_NUM_0) == 0;
            if (low && !pressed) {
                pressed = true;
                down = now();
            }
            if (!low && pressed) {
                pressed = false;
                auto held = now() - down;
                if (held >= 8000) {
                    ESP_LOGW("setup", "Physical reset: erase this adapter's pairing and restart");
                    ESP_ERROR_CHECK(nvs_flash_erase());
                    esp_restart();
                } else if (held >= 2000)
                    open_pairing();
#if CONFIG_BRIDGE_CAR || CONFIG_BRIDGE_SINGLE
                else if (held >= 50)
                    car_test();
#endif
            }
#if CONFIG_BRIDGE_CAR || CONFIG_BRIDGE_SINGLE
            car_poll();
#endif
#if CONFIG_BRIDGE_SINGLE
            bridge::WireMessage message{};
            if (local.pop_for_phone(message)) phone_receive(message);
#endif
#if CONFIG_BRIDGE_PHONE || CONFIG_BRIDGE_SINGLE
            phone_poll();
#endif
#if CONFIG_BRIDGE_SINGLE
            for (size_t i = 0; i < LocalBridge::capacity && local.pop_for_car(message); ++i)
                car_receive(message);
#endif
#if CONFIG_BRIDGE_CALL_RELAY
            relay_poll();
#endif
            if (now() - last_status >= 30000) {
                last_status = now();
                status();
            }
        }
#if !CONFIG_BRIDGE_SINGLE
        bridge::Bytes frame;
        {
            Guard g;
            auto &queue = call_outgoing.empty() ? outgoing : call_outgoing;
            if (!queue.empty()) {
                frame = std::move(queue.front());
                queue.pop_front();
            }
        }
        if (!frame.empty() && uart_write_bytes(UART_NUM_2, frame.data(), frame.size()) != int(frame.size()))
            ESP_LOGW("wire", "UART write failed");
#endif
    }
}
