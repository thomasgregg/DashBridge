#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "runtime.hpp"
#include "sdkconfig.h"
#include <deque>
namespace runtime {
SemaphoreHandle_t mutex;
static std::deque<bridge::Bytes> outgoing;
static int64_t pair_until = 0;
void send(const bridge::WireMessage &m) {
    Guard g;
    if (outgoing.size() < 32)
        outgoing.push_back(bridge::encode(m));
    else
        ESP_LOGW("wire", "Queue full; notification dropped");
}
bool pairing_allowed() {
    return now() < pair_until;
}
void paired() {
    pair_until = 0;
}
static void open_pairing() {
    pair_until = now() + 120000;
    ESP_LOGI("setup", "Pairing open for 120 seconds");
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
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_2, &u));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, 17, 16, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, 4096, 4096, 0, nullptr, 0));
    gpio_config_t b = {};
    b.pin_bit_mask = 1ULL << GPIO_NUM_0;
    b.mode = GPIO_MODE_INPUT;
    b.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&b));
    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BTDM));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
#if CONFIG_BRIDGE_PHONE
    if (esp_ble_get_bond_device_num() == 0)
        open_pairing();
    ESP_LOGI("bridge", "DashBridge A: iPhone receiver. Prototype 0.1, no call/audio relay.");
    phone_start();
#else
    if (esp_bt_gap_get_bond_device_num() == 0)
        open_pairing();
    ESP_LOGI("bridge", "DashBridge B: Tesla receiver. Prototype 0.1, no call/audio relay.");
    car_start();
#endif
    bridge::WireDecoder decoder;
    uint8_t buf[256];
    int64_t down = 0;
    bool pressed = false;
    for (;;) {
        int n = uart_read_bytes(UART_NUM_2, buf, sizeof buf, pdMS_TO_TICKS(20));
        {
            Guard g;
            if (n > 0)
                decoder.feed(buf, n, [](const bridge::WireMessage &m) {
#if CONFIG_BRIDGE_PHONE
                    phone_receive(m);
#else
                car_receive(m);
#endif
                });
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
#if CONFIG_BRIDGE_CAR
                else if (held >= 50)
                    car_test();
#endif
            }
#if CONFIG_BRIDGE_PHONE
            phone_poll();
#else
            car_poll();
#endif
        }
        bridge::Bytes frame;
        {
            Guard g;
            if (!outgoing.empty()) {
                frame = std::move(outgoing.front());
                outgoing.pop_front();
            }
        }
        if (!frame.empty() && uart_write_bytes(UART_NUM_2, frame.data(), frame.size()) != int(frame.size()))
            ESP_LOGW("wire", "UART write failed");
    }
}
