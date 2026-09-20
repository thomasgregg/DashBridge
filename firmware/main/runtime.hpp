#pragma once
#include "bridge_core.hpp"
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
void send(const bridge::WireMessage &m);
bool pairing_allowed();
void paired();
void phone_start();
void phone_poll();
void phone_receive(const bridge::WireMessage &m);
void car_start();
void car_poll();
void car_receive(const bridge::WireMessage &m);
void car_test();
} // namespace runtime
