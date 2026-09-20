#include "sdkconfig.h"
#if CONFIG_BRIDGE_CALL_RELAY
#include "call_relay.hpp"
#include "call_protocol.hpp"
#include <algorithm>
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"
#if CONFIG_BRIDGE_PHONE
#include "esp_hf_client_api.h"
#else
#include "esp_hf_ag_api.h"
#endif
namespace runtime {
static portMUX_TYPE audio_lock = portMUX_INITIALIZER_UNLOCKED;
static calls::PcmBuffer playback;
struct Packet { calls::Frame frame; int64_t created; };
static QueueHandle_t packets;
static uint32_t local_token = 0, remote_token = 0, dropped = 0, received = 0, underruns = 0;
static uint16_t tx_sequence = 0;
static calls::AudioSequence rx_sequence;
static std::array<uint8_t, calls::pcm_size> partial{};
static size_t partial_size = 0;
static int64_t last_rx = 0;
void call_audio_set(uint32_t local, uint32_t remote) {
    portENTER_CRITICAL(&audio_lock);
    if (local != local_token || remote != remote_token) {
        local_token = local; remote_token = remote;
        playback.clear(); partial_size = 0; rx_sequence = {}; last_rx = 0;
    }
    portEXIT_CRITICAL(&audio_lock);
}
void call_audio_in(const uint8_t *data, uint32_t size) {
    // Legacy internal-codec callbacks in pinned IDF 5.5.1 provide PCM for CVSD.
    // At most one 7.5 ms packet is copied per critical section; UART runs in a worker.
    if (size % 2) return;
    while (size) {
        Packet packet{};
        bool complete = false;
        portENTER_CRITICAL(&audio_lock);
        if (!local_token || !remote_token) { portEXIT_CRITICAL(&audio_lock); return; }
        size_t count = std::min(size_t(size), partial.size() - partial_size);
        memcpy(partial.data() + partial_size, data, count);
        partial_size += count; data += count; size -= count;
        if (partial_size == partial.size()) {
            packet.frame = calls::audio_frame(local_token, remote_token, ++tx_sequence, partial.data());
            packet.created = esp_timer_get_time();
            partial_size = 0; complete = true;
        }
        portEXIT_CRITICAL(&audio_lock);
        if (complete && xQueueSend(packets, &packet, 0) != pdTRUE) {
            portENTER_CRITICAL(&audio_lock); ++dropped; portEXIT_CRITICAL(&audio_lock);
        }
    }
}
uint32_t call_audio_out(uint8_t *data, uint32_t size) {
    portENTER_CRITICAL(&audio_lock);
    if (!last_rx || esp_timer_get_time() - last_rx > 60000) playback.clear();
    auto n = local_token && remote_token ? playback.pop(data, size) : 0;
    if (!n && local_token && remote_token) ++underruns;
    portEXIT_CRITICAL(&audio_lock);
    return n;
}
static void worker(void *) {
    calls::AudioDecoder decoder;
    uint8_t bytes[512];
    Packet packet{};
    for (;;) {
        int n = uart_read_bytes(UART_NUM_1, bytes, sizeof bytes, 0);
        bool audio_ready = false;
        for (int i = 0; i < n; ++i) decoder.feed(bytes[i], [&](const calls::Frame &f) {
            portENTER_CRITICAL(&audio_lock);
            if (local_token && remote_token && calls::get32(f.data() + 4) == remote_token &&
                calls::get32(f.data() + 8) == local_token) {
                uint16_t seq = uint16_t(f[12]) | uint16_t(f[13]) << 8;
                unsigned progress = rx_sequence.accept(seq, esp_timer_get_time());
                if (progress) {
                    if (progress == 2) { playback.clear(); ++dropped; }
                    if (!playback.push(f.data() + 14, calls::pcm_size)) ++dropped;
                    last_rx = esp_timer_get_time(); ++received; audio_ready = true;
                }
            }
            portEXIT_CRITICAL(&audio_lock);
        });
        if (audio_ready) {
#if CONFIG_BRIDGE_PHONE
            esp_hf_client_outgoing_data_ready();
#else
            esp_hf_ag_outgoing_data_ready();
#endif
        }
        if (xQueueReceive(packets, &packet, 0) == pdTRUE) {
            portENTER_CRITICAL(&audio_lock);
            bool current = local_token && remote_token && calls::get32(packet.frame.data() + 4) == local_token &&
                calls::get32(packet.frame.data() + 8) == remote_token;
            portEXIT_CRITICAL(&audio_lock);
            if (current && esp_timer_get_time() - packet.created < 60000)
                uart_write_bytes(UART_NUM_1, packet.frame.data(), packet.frame.size());
        }
        vTaskDelay(1);
    }
}
void call_audio_start() {
    uart_config_t config = {};
    config.baud_rate = 921600; config.data_bits = UART_DATA_8_BITS; config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1; config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE; config.source_clk = UART_SCLK_DEFAULT;
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, 25, 26, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 2048, 0, 0, nullptr, 0));
    packets = xQueueCreate(8, sizeof(Packet));
    configASSERT(packets);
    configASSERT(xTaskCreate(worker, "call_audio", 4096, nullptr, 10, nullptr) == pdPASS);
}
void call_audio_status() {
    portENTER_CRITICAL(&audio_lock);
    auto rx = received, drop = dropped, under = underruns;
    bool active = local_token && remote_token;
    portEXIT_CRITICAL(&audio_lock);
    ESP_LOGI("audio", "PCM link: %s; received=%lu dropped=%lu underruns=%lu", active ? "enabled" : "idle",
             (unsigned long)rx, (unsigned long)drop, (unsigned long)under);
}
}
#endif
