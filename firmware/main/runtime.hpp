#pragma once
#include "bridge_core.hpp"
#include "local_bridge.hpp"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
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
void phone_start();
void phone_poll();
void phone_receive(const bridge::WireMessage &m);
bool phone_notifications_ready();
void phone_status();
void car_start();
void car_poll();
void car_receive(const bridge::WireMessage &m);
void car_test();
bool car_notifications_ready();
void contacts_receive(const bridge::WireMessage &m);
bridge::Phonebook &contacts_phonebook();
} // namespace runtime
