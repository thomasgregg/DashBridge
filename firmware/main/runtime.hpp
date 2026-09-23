#pragma once
#include "bridge_core.hpp"
#include "local_bridge.hpp"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string>
namespace runtime {
extern SemaphoreHandle_t mutex;
struct Guard {
    Guard() { xSemaphoreTakeRecursive(mutex, portMAX_DELAY); }
    ~Guard() { xSemaphoreGiveRecursive(mutex); }
};
inline int64_t now() {
    return esp_timer_get_time() / 1000;
}
bool send_to_phone(const bridge::WireMessage &m);
bool send_to_car(const bridge::WireMessage &m);
bool pairing_allowed(Peer peer);
void paired(Peer peer);
void setup_open_pairing(Peer peer);
void setup_ble_start();
void phone_start();
void phone_poll();
void phone_receive(const bridge::WireMessage &m);
bool phone_notifications_ready();
void phone_status();
bool phone_bluetooth_ready();
bool phone_board_link_ready();
bool phone_car_message_ready();
bool phone_car_transport_ready();
bool phone_car_sync_ready();
bool phone_car_call_ready();
void phone_setup_apps();
bool phone_setup_allow(const std::string &id, bridge::Preview preview);
bool phone_setup_deny(const std::string &id);
bool phone_setup_discover();
bool phone_setup_test_notification();
std::string phone_setup_policy_ids();
void setup_emit(const std::string &json);
std::string setup_escape(const std::string &value);
void car_start();
void car_poll();
void car_receive(const bridge::WireMessage &m);
bool car_test();
bool car_notifications_ready();
void contacts_receive(const bridge::WireMessage &m);
bridge::Phonebook &contacts_phonebook();
bool car_message_transport_ready();
bool car_message_sync_ready();
bool car_board_link_ready();
bool car_phone_notification_ready();
bool car_phone_bluetooth_ready();
bool car_phone_call_ready();
} // namespace runtime
