#include "sdkconfig.h"
#if CONFIG_BRIDGE_CALL_RELAY && CONFIG_BT_HFP_USE_EXTERNAL_CODEC
#include "dashbridge/adapters/features.hpp"
#include "dashbridge/adapters/call_audio_test.hpp"
#include "dashbridge/protocols/calls_v3.hpp"
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
static calls::EncodedJitterBuffer playback;
static QueueHandle_t packets;
#if CONFIG_BRIDGE_MUSIC_RELAY
static QueueHandle_t music_packets;
#endif
static QueueHandle_t uart_events;
static uint32_t local_token = 0, remote_token = 0;
static unsigned sample_rate = 0;
static uint16_t sync_handle = 0xffff, tx_sequence = 0;
static int64_t last_rx = 0;
static calls::AudioSequence rx_sequence;
static uint32_t sent = 0, received = 0, dropped = 0, startup_waits = 0;
static uint32_t input_bytes = 0, output_bytes = 0, tx_full = 0, rx_gaps = 0, rx_overflow = 0;
static uint32_t bad_radio_frames = 0, codec_mismatch = 0, send_failures = 0;
static uint32_t concealed_radio = 0, concealed_clock = 0, clock_trims = 0;
static uint32_t uart_overruns = 0, uart_frame_errors = 0, wire_crc_errors = 0;
#if CONFIG_BRIDGE_MUSIC_RELAY
static uint32_t music_wire_crc_errors = 0, music_wire_malformed = 0, music_tx_full = 0;
#endif

struct Packet {
    calls::EncodedAudio audio;
    uint32_t source = 0, target = 0;
    uint16_t sequence = 0;
    int64_t created = 0;
};

static calls::AudioCodec local_codec() {
    return sample_rate == 8000 ? calls::AudioCodec::cvsd : calls::AudioCodec::msbc;
}
static void clear_audio_buffers() {
    playback.clear(); rx_sequence = {}; last_rx = 0;
}
bool call_audio_test(calls::AudioTestMode mode) {
    // Tone and PCM loopback deliberately remain unavailable on the encoded path:
    // this build changes the production relay rather than adding another test mode.
    return mode == calls::AudioTestMode::normal;
}
void call_audio_set(uint32_t local, uint32_t remote, unsigned rate) {
    portENTER_CRITICAL(&audio_lock);
    if (local != local_token || remote != remote_token || rate != sample_rate) {
        local_token = local; remote_token = remote; sample_rate = rate;
        if (!local || (rate != 8000 && rate != 16000)) sync_handle = 0xffff;
        clear_audio_buffers();
    }
    portEXIT_CRITICAL(&audio_lock);
}
void call_audio_connection(uint16_t handle) {
    portENTER_CRITICAL(&audio_lock);
    sync_handle = handle;
    clear_audio_buffers();
    portEXIT_CRITICAL(&audio_lock);
}

static void free_audio(esp_hf_audio_buff_t *audio) {
#if CONFIG_BRIDGE_PHONE
    esp_hf_client_audio_buff_free(audio);
#else
    esp_hf_ag_audio_buff_free(audio);
#endif
}
static esp_hf_audio_buff_t *allocate_audio(uint16_t size) {
#if CONFIG_BRIDGE_PHONE
    return esp_hf_client_audio_buff_alloc(size);
#else
    return esp_hf_ag_audio_buff_alloc(size);
#endif
}
static esp_err_t send_audio(uint16_t handle, esp_hf_audio_buff_t *audio) {
#if CONFIG_BRIDGE_PHONE
    return esp_hf_client_audio_data_send(handle, audio);
#else
    return esp_hf_ag_audio_data_send(handle, audio);
#endif
}

#if CONFIG_BRIDGE_MUSIC_RELAY
bool music_audio_transport_send(const music::Audio &audio) {
    if (!music_audio_allowed() || !music_packets || !audio.stream || !audio.size ||
        audio.size > music::max_payload)
        return false;
    if (xQueueSend(music_packets, &audio, 0) == pdTRUE)
        return true;
    portENTER_CRITICAL(&audio_lock);
    ++music_tx_full;
    portEXIT_CRITICAL(&audio_lock);
    return false;
}
#endif

static void incoming_audio(esp_hf_sync_conn_hdl_t handle, esp_hf_audio_buff_t *buffer, bool bad) {
    Packet packet;
    calls::EncodedAudio outgoing;
    bool have_packet = false, have_outgoing = false;
    const int64_t timestamp = esp_timer_get_time();

    portENTER_CRITICAL(&audio_lock);
    const bool current = local_token && remote_token && handle == sync_handle &&
        (sample_rate == 8000 || sample_rate == 16000);
    if (bad) ++bad_radio_frames;
    if (current) {
        if (bad && local_codec() == calls::AudioCodec::msbc) {
            packet.audio = calls::msbc_silence_audio(); ++concealed_radio;
        } else if (!bad && buffer && buffer->data && buffer->data_len) {
            packet.audio.codec = local_codec();
            packet.audio.size = std::min<uint16_t>(buffer->data_len,
                packet.audio.codec == calls::AudioCodec::msbc ? ESP_HF_MSBC_ENCODED_FRAME_SIZE : calls::encoded_audio_size);
            if (packet.audio.size) memcpy(packet.audio.data.data(), buffer->data, packet.audio.size);
        }
        if (packet.audio.size) {
            packet.source = local_token; packet.target = remote_token;
            packet.sequence = ++tx_sequence; packet.created = timestamp;
            input_bytes += packet.audio.size; have_packet = true;
        }
        if (last_rx && timestamp - last_rx > 60000) clear_audio_buffers();
        size_t trimmed = 0;
        const auto playout = playback.pop(local_codec(), outgoing, trimmed);
        clock_trims += trimmed;
        if (playout == calls::EncodedPlayout::audio) {
            have_outgoing = true;
        } else if (playout == calls::EncodedPlayout::conceal && local_codec() == calls::AudioCodec::msbc) {
            outgoing = calls::msbc_silence_audio(); have_outgoing = true; ++concealed_clock;
        } else if (playout == calls::EncodedPlayout::codec_mismatch) {
            ++codec_mismatch;
        } else {
            ++startup_waits;
        }
    }
    portEXIT_CRITICAL(&audio_lock);

    if (buffer) free_audio(buffer);
    if (have_packet && xQueueSend(packets, &packet, 0) != pdTRUE) {
        portENTER_CRITICAL(&audio_lock); ++dropped; ++tx_full; portEXIT_CRITICAL(&audio_lock);
    }
    if (!have_outgoing) return;
    auto *audio = allocate_audio(outgoing.size);
    if (!audio) {
        portENTER_CRITICAL(&audio_lock); ++dropped; ++send_failures; portEXIT_CRITICAL(&audio_lock);
        return;
    }
    memcpy(audio->data, outgoing.data.data(), outgoing.size);
    audio->data_len = outgoing.size;
    if (send_audio(handle, audio) == ESP_OK) {
        portENTER_CRITICAL(&audio_lock); output_bytes += outgoing.size; portEXIT_CRITICAL(&audio_lock);
    } else {
        free_audio(audio);
        portENTER_CRITICAL(&audio_lock); ++dropped; ++send_failures; portEXIT_CRITICAL(&audio_lock);
    }
}

static void worker(void *) {
    calls::EncodedAudioDecoder decoder;
#if CONFIG_BRIDGE_MUSIC_RELAY
    music::Decoder music_decoder;
#endif
    uint8_t bytes[512];
    Packet packet;
#if CONFIG_BRIDGE_MUSIC_RELAY
    music::Audio music_packet;
    music::Frame music_frame;
#endif
    for (;;) {
        uart_event_t event;
        while (xQueueReceive(uart_events, &event, 0) == pdTRUE) {
            portENTER_CRITICAL(&audio_lock);
            if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) ++uart_overruns;
            if (event.type == UART_FRAME_ERR || event.type == UART_PARITY_ERR) ++uart_frame_errors;
            portEXIT_CRITICAL(&audio_lock);
        }
        int n = uart_read_bytes(UART_NUM_1, bytes, sizeof bytes, 0);
        for (int i = 0; i < n; ++i) {
            decoder.feed(bytes[i], [&](const calls::EncodedFrame &frame) {
            portENTER_CRITICAL(&audio_lock);
            if (local_token && remote_token && calls::get32(frame.data() + 4) == remote_token &&
                calls::get32(frame.data() + 8) == local_token) {
                const uint16_t sequence = uint16_t(frame[12]) | uint16_t(frame[13]) << 8;
                const bool had_sequence = rx_sequence.have;
                const unsigned progress = rx_sequence.accept(sequence, esp_timer_get_time());
                if (progress) {
                    if (progress == 2) {
                        playback.clear();
                        if (had_sequence) { ++dropped; ++rx_gaps; }
                    }
                    calls::EncodedAudio audio;
                    audio.codec = calls::AudioCodec(frame[14]); audio.size = frame[15];
                    memcpy(audio.data.data(), frame.data() + 16, audio.size);
                    if (audio.codec != local_codec()) {
                        playback.clear(); ++dropped; ++codec_mismatch;
                    } else if (!playback.push(audio)) {
                        ++dropped; ++rx_overflow;
                    }
                    last_rx = esp_timer_get_time(); ++received;
                }
            }
            portEXIT_CRITICAL(&audio_lock);
            });
#if CONFIG_BRIDGE_MUSIC_RELAY
            music_decoder.feed(bytes[i], [](const music::Audio &audio) {
                music_audio_transport_receive(audio);
            });
#endif
        }
        portENTER_CRITICAL(&audio_lock); wire_crc_errors = decoder.crc_failures(); portEXIT_CRITICAL(&audio_lock);
#if CONFIG_BRIDGE_MUSIC_RELAY
        portENTER_CRITICAL(&audio_lock);
        music_wire_crc_errors = music_decoder.crc_failures();
        music_wire_malformed = music_decoder.malformed();
        portEXIT_CRITICAL(&audio_lock);
#endif

        if (xQueueReceive(packets, &packet, 0) == pdTRUE) {
            portENTER_CRITICAL(&audio_lock);
            const bool current = local_token && remote_token && packet.source == local_token &&
                packet.target == remote_token && esp_timer_get_time() - packet.created < 60000;
            portEXIT_CRITICAL(&audio_lock);
            if (current) {
                const auto frame = calls::encoded_audio_frame(packet.source, packet.target, packet.sequence, packet.audio);
                uart_write_bytes(UART_NUM_1, frame.data(), frame.size());
                portENTER_CRITICAL(&audio_lock); ++sent; portEXIT_CRITICAL(&audio_lock);
            } else {
                portENTER_CRITICAL(&audio_lock); ++dropped; portEXIT_CRITICAL(&audio_lock);
            }
        }
#if CONFIG_BRIDGE_MUSIC_RELAY
        else if (xQueueReceive(music_packets, &music_packet, 0) == pdTRUE) {
            // Dequeue while calls own focus so pre-call audio cannot play after
            // the call ends. New music frames are rejected at enqueue time too.
            if (music_audio_allowed()) {
                const size_t size = music::encode(music_packet, music_frame);
                if (size)
                    uart_write_bytes(UART_NUM_1, music_frame.data(), size);
            }
        }
#endif
        vTaskDelay(1);
    }
}

void call_audio_start() {
    uart_config_t config = {};
    config.baud_rate = calls::audio_baud; config.data_bits = UART_DATA_8_BITS; config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1; config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE; config.source_clk = UART_SCLK_DEFAULT;
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, 25, 26, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 4096, 4096, 16, &uart_events, 0));
    ESP_ERROR_CHECK(uart_set_rx_full_threshold(UART_NUM_1, 32));
    packets = xQueueCreate(12, sizeof(Packet));
    configASSERT(packets);
#if CONFIG_BRIDGE_MUSIC_RELAY
    music_packets = xQueueCreate(8, sizeof(music::Audio));
    configASSERT(music_packets);
#endif
    // Music adds two ~1 KiB frame buffers and a ~1 KiB decoder to this task's
    // existing audio/UART locals. Four KiB overflows before the first loop.
    configASSERT(xTaskCreate(worker, "call_audio", 8192, nullptr, 10, nullptr) == pdPASS);
}
void call_audio_register() {
#if CONFIG_BRIDGE_PHONE
    ESP_ERROR_CHECK(esp_hf_client_register_audio_data_callback(incoming_audio));
#else
    ESP_ERROR_CHECK(esp_hf_ag_register_audio_data_callback(incoming_audio));
#endif
}
void call_audio_status() {
    portENTER_CRITICAL(&audio_lock);
    const bool active = local_token && remote_token; const auto rate = sample_rate;
    const auto rx = received, drop = dropped, waits = startup_waits;
    portEXIT_CRITICAL(&audio_lock);
    ESP_LOGI("audio", "Encoded link: %s; local=%u Hz (%s); wire=codec-transparent v1; received=%lu dropped=%lu startup_wait=%lu",
             active ? "enabled" : "idle", rate, rate == 16000 ? "mSBC HD" : (rate == 8000 ? "CVSD" : "off"),
             (unsigned long)rx, (unsigned long)drop, (unsigned long)waits);
    ESP_LOGI("audio", "Local audio counters: %s", call_audio_diagnostics().c_str());
}
std::string call_audio_diagnostics() {
    portENTER_CRITICAL(&audio_lock);
    const auto rate = sample_rate;
    const auto in = input_bytes, out = output_bytes, tx = sent, rx = received;
    const auto drop = dropped, full = tx_full, gaps = rx_gaps, overflow = rx_overflow;
    const auto bad = bad_radio_frames, mismatch = codec_mismatch, send_fail = send_failures;
    const auto radio_fill = concealed_radio, clock_fill = concealed_clock, trims = clock_trims;
    const auto overruns = uart_overruns, framing = uart_frame_errors, crc = wire_crc_errors;
#if CONFIG_BRIDGE_MUSIC_RELAY
    const auto media_crc = music_wire_crc_errors, media_bad = music_wire_malformed, media_full = music_tx_full;
#endif
    const auto queued = playback.size();
    portEXIT_CRITICAL(&audio_lock);
    char text[512];
    snprintf(text, sizeof text,
             "path=encoded rate=%u baud=%u in=%lu out=%lu tx=%lu rx=%lu dropped=%lu tx_full=%lu rx_gaps=%lu rx_overflow=%lu bad_radio=%lu conceal_radio=%lu conceal_clock=%lu clock_trim=%lu codec_mismatch=%lu send_fail=%lu jitter=%u uart_overrun=%lu uart_frame=%lu crc=%lu"
#if CONFIG_BRIDGE_MUSIC_RELAY
             " music_crc=%lu music_bad=%lu music_tx_full=%lu"
#endif
             " mode=normal",
             rate, calls::audio_baud, (unsigned long)in, (unsigned long)out, (unsigned long)tx, (unsigned long)rx,
             (unsigned long)drop, (unsigned long)full, (unsigned long)gaps, (unsigned long)overflow,
             (unsigned long)bad, (unsigned long)radio_fill, (unsigned long)clock_fill, (unsigned long)trims,
             (unsigned long)mismatch, (unsigned long)send_fail, unsigned(queued),
             (unsigned long)overruns, (unsigned long)framing, (unsigned long)crc
#if CONFIG_BRIDGE_MUSIC_RELAY
             , (unsigned long)media_crc, (unsigned long)media_bad, (unsigned long)media_full
#endif
             );
    return text;
}
} // namespace runtime
#endif
