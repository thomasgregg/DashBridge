#include "sdkconfig.h"
#if CONFIG_BRIDGE_CALL_RELAY
#include "call_relay.hpp"
#include "call_protocol.hpp"
#include "call_resampler.hpp"
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
static calls::PcmUpsampler upsampler;
static calls::PcmDownsampler downsampler;
static unsigned sample_rate = 0;
static calls::AudioTest audio_test;
static int64_t mode_changed_at = 0;
struct Packet { calls::Frame frame; int64_t created; };
static QueueHandle_t packets;
static QueueHandle_t uart_events;
static uint32_t local_token = 0, remote_token = 0, dropped = 0, received = 0, underruns = 0;
static uint32_t sent = 0, input_bytes = 0, output_bytes = 0;
static uint32_t tx_full = 0, tx_stale = 0, rx_gaps = 0, rx_overflow = 0;
static unsigned input_peak = 0;
static uint32_t uart_overruns = 0, uart_frame_errors = 0, wire_crc_errors = 0;
static uint16_t tx_sequence = 0;
static calls::AudioSequence rx_sequence;
static std::array<uint8_t, calls::pcm_size> partial{};
static size_t partial_size = 0;
static int64_t last_rx = 0;
static void clear_audio_buffers() {
    upsampler.clear(); downsampler.clear(); playback.clear();
    partial_size = 0; rx_sequence = {}; last_rx = 0;
    mode_changed_at = esp_timer_get_time();
}
static bool expire_audio_test() {
    if (!audio_test.expire(esp_timer_get_time())) return false;
    clear_audio_buffers(); return true;
}
bool call_audio_test(calls::AudioTestMode mode) {
    portENTER_CRITICAL(&audio_lock);
    bool accepted = mode == calls::AudioTestMode::normal || local_token;
    if (accepted) accepted = audio_test.start(mode, sample_rate, esp_timer_get_time());
    if (accepted) clear_audio_buffers();
    portEXIT_CRITICAL(&audio_lock);
    return accepted;
}
void call_audio_set(uint32_t local, uint32_t remote, unsigned rate) {
    portENTER_CRITICAL(&audio_lock);
    if (local != local_token || remote != remote_token || rate != sample_rate) {
        if (local != local_token || rate != sample_rate) audio_test.stop();
        local_token = local; remote_token = remote;
        sample_rate = rate;
        input_peak = 0;
        clear_audio_buffers();
    }
    portEXIT_CRITICAL(&audio_lock);
}
void call_audio_connection(uint16_t) {}
void call_audio_in(const uint8_t *data, uint32_t size) {
    // IDF 5.5.5 internal codecs supply decoded int16 PCM: CVSD 8 kHz, mSBC 16 kHz.
    // At most one 7.5 ms packet is copied per critical section; UART runs in a worker.
    if (size % 2) return;
    unsigned peak = 0;
    for (uint32_t i = 0; i < size; i += 2) {
        const int value = calls::pcm_read(data + i);
        peak = std::max(peak, unsigned(value < 0 ? -value : value));
    }
    portENTER_CRITICAL(&audio_lock);
    expire_audio_test();
    input_bytes += size; input_peak = std::max(input_peak, peak);
    if (audio_test.mode() != calls::AudioTestMode::normal) {
        audio_test.input(data, size, esp_timer_get_time());
        portEXIT_CRITICAL(&audio_lock); return;
    }
    portEXIT_CRITICAL(&audio_lock);
    while (size) {
        Packet packet{};
        std::array<uint8_t, calls::pcm_size> completed_pcm{};
        uint32_t source = 0, target = 0;
        uint16_t sequence = 0;
        bool complete = false;
        portENTER_CRITICAL(&audio_lock);
        if (audio_test.mode() != calls::AudioTestMode::normal || !local_token || !remote_token || (sample_rate != 8000 && sample_rate != 16000)) {
            portEXIT_CRITICAL(&audio_lock); return;
        }
        const unsigned expansion = sample_rate == 8000 ? 2 : 1;
        size_t count = std::min(size_t(size), (partial.size() - partial_size) / expansion);
        if (expansion == 2) upsampler.convert(data, count, partial.data() + partial_size);
        else memcpy(partial.data() + partial_size, data, count);
        partial_size += count * expansion; data += count; size -= count;
        if (partial_size == partial.size()) {
            source = local_token; target = remote_token; sequence = ++tx_sequence;
            memcpy(completed_pcm.data(), partial.data(), partial.size());
            packet.created = esp_timer_get_time();
            partial_size = 0; complete = true;
        }
        portEXIT_CRITICAL(&audio_lock);
        // CRC work must not mask UART RX interrupts. Tokens captured with the
        // PCM let the worker reject this packet if the stream changes meanwhile.
        if (complete) packet.frame = calls::audio_frame(source, target, sequence, completed_pcm.data());
        if (complete && xQueueSend(packets, &packet, 0) != pdTRUE) {
            portENTER_CRITICAL(&audio_lock); ++dropped; ++tx_full; portEXIT_CRITICAL(&audio_lock);
        }
    }
}
uint32_t call_audio_out(uint8_t *data, uint32_t size) {
    std::array<uint8_t, calls::pcm_size * 2> narrowband_input{};
    portENTER_CRITICAL(&audio_lock);
    expire_audio_test();
    if (audio_test.mode() != calls::AudioTestMode::normal) {
        auto n = audio_test.output(data, size, esp_timer_get_time());
        output_bytes += n;
        portEXIT_CRITICAL(&audio_lock); return n;
    }
    if (!last_rx || esp_timer_get_time() - last_rx > 60000) {
        playback.clear(); downsampler.clear();
    }
    const bool narrowband = sample_rate == 8000;
    if (!size || size % 2 || size > calls::pcm_size ||
        (sample_rate != 8000 && sample_rate != 16000)) {
        portEXIT_CRITICAL(&audio_lock); return 0;
    }
    auto n = local_token && remote_token ? playback.pop(narrowband ? narrowband_input.data() : data,
                                                       narrowband ? size * 2 : size) : 0;
    if (narrowband && n) {
        downsampler.convert(narrowband_input.data(), n, data);
        n /= 2;
    }
    // A normal drain-until-empty probe must preserve the CVSD filter history.
    if (!n && local_token && remote_token) ++underruns;
    output_bytes += n;
    portEXIT_CRITICAL(&audio_lock);
    return n;
}
static void worker(void *) {
    calls::AudioDecoder decoder;
    uint8_t bytes[512];
    Packet packet{};
    for (;;) {
        uart_event_t event;
        while (xQueueReceive(uart_events, &event, 0) == pdTRUE) {
            portENTER_CRITICAL(&audio_lock);
            if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) ++uart_overruns;
            if (event.type == UART_FRAME_ERR || event.type == UART_PARITY_ERR) ++uart_frame_errors;
            portEXIT_CRITICAL(&audio_lock);
        }
        int n = uart_read_bytes(UART_NUM_1, bytes, sizeof bytes, 0);
        bool audio_ready = false;
        for (int i = 0; i < n; ++i) decoder.feed(bytes[i], [&](const calls::Frame &f) {
            portENTER_CRITICAL(&audio_lock);
            if (local_token && remote_token && calls::get32(f.data() + 4) == remote_token &&
                calls::get32(f.data() + 8) == local_token) {
                uint16_t seq = uint16_t(f[12]) | uint16_t(f[13]) << 8;
                bool had_sequence = rx_sequence.have;
                unsigned progress = rx_sequence.accept(seq, esp_timer_get_time());
                if (progress) {
                    if (progress == 2) {
                        playback.clear(); downsampler.clear();
                        if (had_sequence) { ++dropped; ++rx_gaps; }
                    }
                    if (audio_test.mode() == calls::AudioTestMode::normal && !playback.push(f.data() + 14, calls::pcm_size)) {
                        downsampler.clear(); ++dropped; ++rx_overflow;
                    }
                    last_rx = esp_timer_get_time(); ++received; audio_ready = true;
                }
            }
            portEXIT_CRITICAL(&audio_lock);
        });
        portENTER_CRITICAL(&audio_lock);
        wire_crc_errors = decoder.crc_failures();
        bool expired = expire_audio_test();
        if (audio_test.mode() != calls::AudioTestMode::normal)
            audio_ready = audio_test.tick(esp_timer_get_time());
        portEXIT_CRITICAL(&audio_lock);
        if (expired) ESP_LOGI("audio", "Audio test expired; normal relay restored");
        if (audio_ready) {
#if CONFIG_BRIDGE_PHONE
            esp_hf_client_outgoing_data_ready();
#else
            esp_hf_ag_outgoing_data_ready();
#endif
        }
        if (xQueueReceive(packets, &packet, 0) == pdTRUE) {
            portENTER_CRITICAL(&audio_lock);
            bool current = audio_test.mode() == calls::AudioTestMode::normal && packet.created >= mode_changed_at &&
                local_token && remote_token && calls::get32(packet.frame.data() + 4) == local_token &&
                calls::get32(packet.frame.data() + 8) == remote_token;
            portEXIT_CRITICAL(&audio_lock);
            if (current && esp_timer_get_time() - packet.created < 60000) {
                uart_write_bytes(UART_NUM_1, packet.frame.data(), packet.frame.size());
                portENTER_CRITICAL(&audio_lock); ++sent; portEXIT_CRITICAL(&audio_lock);
            } else if (current) {
                portENTER_CRITICAL(&audio_lock); ++dropped; ++tx_stale; portEXIT_CRITICAL(&audio_lock);
            }
        }
        vTaskDelay(1);
    }
}
void call_audio_start() {
    uart_config_t config = {};
    config.baud_rate = calls::audio_baud; config.data_bits = UART_DATA_8_BITS; config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1; config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE; config.source_clk = UART_SCLK_DEFAULT;
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, 25, 26, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 2048, 0, 16, &uart_events, 0));
    // Service RX halfway through the 128-byte FIFO. The default near-full
    // threshold leaves little time for the ISR while Bluetooth is busy.
    ESP_ERROR_CHECK(uart_set_rx_full_threshold(UART_NUM_1, 64));
    packets = xQueueCreate(8, sizeof(Packet));
    configASSERT(packets);
    configASSERT(xTaskCreate(worker, "call_audio", 4096, nullptr, 10, nullptr) == pdPASS);
}
void call_audio_status() {
    portENTER_CRITICAL(&audio_lock);
    auto rx = received, drop = dropped, under = underruns;
    bool active = local_token && remote_token;
    auto rate = sample_rate;
    portEXIT_CRITICAL(&audio_lock);
    ESP_LOGI("audio", "PCM link: %s; local=%u Hz (%s); wire=16000 Hz v2; received=%lu dropped=%lu empty_reads=%lu",
             active ? "enabled" : "idle", rate, rate == 16000 ? "mSBC HD" : (rate == 8000 ? "CVSD" : "off"),
             (unsigned long)rx, (unsigned long)drop, (unsigned long)under);
    ESP_LOGI("audio", "Local audio counters: %s", call_audio_diagnostics().c_str());
}
std::string call_audio_diagnostics() {
    // Copy counters under lock; format outside the Bluetooth critical section.
    portENTER_CRITICAL(&audio_lock);
    const auto rate = sample_rate, peak = input_peak;
    const auto mode = audio_test.mode();
    const auto remaining = audio_test.remaining_ms(esp_timer_get_time());
    const auto test_overrun = audio_test.overruns(), test_late = audio_test.late();
    const auto in = input_bytes, out = output_bytes, tx = sent, rx = received;
    const auto full = tx_full, stale = tx_stale, gaps = rx_gaps, overflow = rx_overflow;
    const auto overruns = uart_overruns, framing = uart_frame_errors, crc = wire_crc_errors;
    portEXIT_CRITICAL(&audio_lock);
    char text[512];
    snprintf(text, sizeof text, "rate=%u baud=%u in=%lu out=%lu peak=%u tx=%lu rx=%lu tx_full=%lu tx_stale=%lu rx_gaps=%lu rx_overflow=%lu uart_overrun=%lu uart_frame=%lu crc=%lu mode=%s test_ms=%lld test_overrun=%lu test_late=%lu",
             rate, calls::audio_baud, (unsigned long)in, (unsigned long)out, peak, (unsigned long)tx, (unsigned long)rx,
             (unsigned long)full, (unsigned long)stale, (unsigned long)gaps, (unsigned long)overflow,
             (unsigned long)overruns, (unsigned long)framing, (unsigned long)crc, calls::audio_test_name(mode),
             (long long)remaining, (unsigned long)test_overrun, (unsigned long)test_late);
    return text;
}
}
#endif
