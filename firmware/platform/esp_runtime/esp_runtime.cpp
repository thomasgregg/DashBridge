#include "dashbridge/platform/esp_runtime.hpp"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gap_bt_api.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <cstdarg>
#include <cstring>

namespace dashbridge::platform {

void EspRuntime::initialize() {
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        error = nvs_flash_init();
    }
    ESP_ERROR_CHECK(error);
    mutex_ = xSemaphoreCreateRecursiveMutex();
    configASSERT(mutex_);

    uart_config_t uart = {};
    uart.baud_rate = 115200;
    uart.data_bits = UART_DATA_8_BITS;
    uart.parity = UART_PARITY_DISABLE;
    uart.stop_bits = UART_STOP_BITS_1;
    uart.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart.source_clk = UART_SCLK_DEFAULT;
#if !CONFIG_BRIDGE_SINGLE
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_2, &uart));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, 17, 16, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, 4096, 4096, 0, nullptr, 0));
#endif
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 512, 0, 0, nullptr, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &uart));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_0, 1, 3, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    gpio_config_t button = {};
    button.pin_bit_mask = 1ULL << GPIO_NUM_0;
    button.mode = GPIO_MODE_INPUT;
    button.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&button));

#if CONFIG_BRIDGE_CAR
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));
#endif
    esp_bt_controller_config_t config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&config));
#if CONFIG_BRIDGE_CAR
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    ESP_LOGI("bridge", "Board B controller mode: Classic only (HFP + MAP)");
#else
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BTDM));
#endif
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
}

int64_t EspRuntime::now_ms() const { return esp_timer_get_time() / 1000; }
uint32_t EspRuntime::random_token() const {
    uint32_t value;
    do value = esp_random(); while (!value);
    return value;
}
bool EspRuntime::classic_bond_known(const uint8_t *address) const {
    int count = esp_bt_gap_get_bond_device_num();
    esp_bd_addr_t bonds[16];
    if (count < 1 || count > 16 || esp_bt_gap_get_bond_device_list(&count, bonds) != ESP_OK)
        return false;
    for (int index = 0; index < count; ++index)
        if (!memcmp(bonds[index], address, sizeof(esp_bd_addr_t)))
            return true;
    return false;
}
void EspRuntime::lock() { xSemaphoreTakeRecursive(static_cast<SemaphoreHandle_t>(mutex_), portMAX_DELAY); }
void EspRuntime::unlock() { xSemaphoreGiveRecursive(static_cast<SemaphoreHandle_t>(mutex_)); }
int EspRuntime::read_control(uint8_t *data, size_t size, uint32_t timeout_ms) {
#if CONFIG_BRIDGE_SINGLE
    (void)data; (void)size; (void)timeout_ms;
    return 0;
#else
    return uart_read_bytes(UART_NUM_2, data, size, pdMS_TO_TICKS(timeout_ms));
#endif
}
bool EspRuntime::write_control(const uint8_t *data, size_t size) {
#if CONFIG_BRIDGE_SINGLE
    (void)data; (void)size;
    return false;
#else
    return uart_write_bytes(UART_NUM_2, data, size) == int(size);
#endif
}
int EspRuntime::read_console(uint8_t *data, size_t size) { return uart_read_bytes(UART_NUM_0, data, size, 0); }
void EspRuntime::write_console(std::string_view text) { uart_write_bytes(UART_NUM_0, text.data(), text.size()); }
bool EspRuntime::button_pressed() const { return gpio_get_level(GPIO_NUM_0) == 0; }
void EspRuntime::delay(uint32_t milliseconds) const { vTaskDelay(pdMS_TO_TICKS(milliseconds)); }
void EspRuntime::erase_pairing_and_restart() { ESP_ERROR_CHECK(nvs_flash_erase()); esp_restart(); }
bool EspRuntime::has_ble_bond() const { return esp_ble_get_bond_device_num() != 0; }
bool EspRuntime::has_classic_bond() const { return esp_bt_gap_get_bond_device_num() != 0; }
const char *EspRuntime::firmware_version() const { return esp_app_get_description()->version; }
void EspRuntime::log_memory() const {
    ESP_LOGI("status", "Internal RAM: free=%u minimum=%u largest=%u bytes",
             unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
}
void EspRuntime::info(const char *tag, const char *format, ...) const {
    va_list args; va_start(args, format); esp_log_writev(ESP_LOG_INFO, tag, format, args); va_end(args);
}
void EspRuntime::warn(const char *tag, const char *format, ...) const {
    va_list args; va_start(args, format); esp_log_writev(ESP_LOG_WARN, tag, format, args); va_end(args);
}

} // namespace dashbridge::platform
