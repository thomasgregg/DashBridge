#include "sdkconfig.h"
#if CONFIG_BRIDGE_MUSIC_RELAY

#include "dashbridge/adapters/features.hpp"
#include "dashbridge/core/music.hpp"
#include "dashbridge/protocols/dashlink_v2.hpp"
#include "dashbridge/ports/runtime_services.hpp"
#include "dashbridge/core/text.hpp"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "esp_log.h"
#include <algorithm>
#include <cstring>
#include <utility>

namespace runtime {
using namespace dashbridge::ports;
namespace connections = dashbridge::core::connections;
namespace dashlink = dashbridge::protocols::dashlink_v2;
namespace {
namespace music_core = dashbridge::core::music;
constexpr const char *tag = "music";
static esp_bd_addr_t peer = {};
static bool peer_known = false, profile_ready = false, connecting = false;
static bool linked = false, streaming = false, codec_ready = false, start_pending = false;
static esp_a2d_conn_hdl_t connection = 0;
static uint16_t connection_mtu = 0, sequence = 0;
static uint32_t stream = 0;
static int64_t next_connect = 0, connected_at = 0;
static uint32_t rx_packets = 0, tx_packets = 0, dropped = 0, send_failures = 0;
static bool avrc_linked = false;
static uint32_t media_updates = 0, control_commands = 0, control_failures = 0;
static music_core::Controller music_controller;
static music_core::State &media_state() {
#if CONFIG_BRIDGE_PHONE
    return music_controller.local();
#else
    return music_controller.remote();
#endif
}
#if CONFIG_BRIDGE_PHONE
static uint8_t transaction_label = 0;
static int64_t last_media_send = 0;
#else
static uint32_t control_session = 0, control_sequence = 0;
static bool notification_registered[ESP_AVRC_RN_MAX_EVT] = {};
#endif

#if CONFIG_BRIDGE_PHONE
static uint8_t next_transaction() {
    const uint8_t value = transaction_label;
    transaction_label = (transaction_label + 1) & 0x0f;
    return value;
}
#endif

static bool supported_control(uint8_t key) {
    return key == ESP_AVRC_PT_CMD_PLAY || key == ESP_AVRC_PT_CMD_PAUSE ||
           key == ESP_AVRC_PT_CMD_STOP || key == ESP_AVRC_PT_CMD_FORWARD ||
           key == ESP_AVRC_PT_CMD_BACKWARD || key == ESP_AVRC_PT_CMD_FAST_FORWARD ||
           key == ESP_AVRC_PT_CMD_REWIND;
}

#if CONFIG_BRIDGE_PHONE
static void send_media_state() {
    if (send_to_car(dashlink::MusicState{media_state()})) {
        ++media_updates;
        last_media_send = now();
    } else {
        ++control_failures;
    }
}

static void request_metadata() {
    const uint8_t attributes = ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST |
        ESP_AVRC_MD_ATTR_ALBUM | ESP_AVRC_MD_ATTR_TRACK_NUM | ESP_AVRC_MD_ATTR_NUM_TRACKS |
        ESP_AVRC_MD_ATTR_GENRE | ESP_AVRC_MD_ATTR_PLAYING_TIME;
    if (esp_avrc_ct_send_metadata_cmd(next_transaction(), attributes) != ESP_OK)
        ++control_failures;
    if (esp_avrc_ct_send_get_play_status_cmd(next_transaction()) != ESP_OK)
        ++control_failures;
}

static void register_notification(uint8_t event_id, uint32_t parameter = 0) {
    if (esp_avrc_ct_send_register_notification_cmd(next_transaction(), event_id, parameter) != ESP_OK)
        ++control_failures;
}

static void controller_event(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param) {
    Guard guard;
    switch (event) {
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
        avrc_linked = param->conn_stat.connected;
        ESP_LOGI(tag, "AVRCP iPhone control: %s", avrc_linked ? "connected" : "disconnected");
        if (avrc_linked) {
            if (esp_avrc_ct_send_get_rn_capabilities_cmd(next_transaction()) != ESP_OK)
                ++control_failures;
            request_metadata();
        }
        break;
    case ESP_AVRC_CT_METADATA_RSP_EVT: {
        const size_t length = std::min<size_t>(size_t(std::max(0, param->meta_rsp.attr_length)),
                                                music_core::maximum_metadata_length);
        const std::string value = param->meta_rsp.attr_text && length
            ? std::string(reinterpret_cast<const char *>(param->meta_rsp.attr_text), length)
            : std::string();
        switch (param->meta_rsp.attr_id) {
        case ESP_AVRC_MD_ATTR_TITLE: media_state().title = value; break;
        case ESP_AVRC_MD_ATTR_ARTIST: media_state().artist = value; break;
        case ESP_AVRC_MD_ATTR_ALBUM: media_state().album = value; break;
        case ESP_AVRC_MD_ATTR_TRACK_NUM: media_state().track = value; break;
        case ESP_AVRC_MD_ATTR_NUM_TRACKS: media_state().track_count = value; break;
        case ESP_AVRC_MD_ATTR_GENRE: media_state().genre = value; break;
        case ESP_AVRC_MD_ATTR_PLAYING_TIME: {
            uint32_t duration = 0;
            if (dashbridge::core::parse_uint32(value, duration)) media_state().length_ms = duration;
            break;
        }
        default: break;
        }
        send_media_state();
        break;
    }
    case ESP_AVRC_CT_PLAY_STATUS_RSP_EVT:
        media_state().length_ms = param->play_status_rsp.song_length;
        media_state().position_ms = param->play_status_rsp.song_position;
        media_state().playback = param->play_status_rsp.play_status;
        send_media_state();
        break;
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT:
        if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST,
                                               &param->get_rn_caps_rsp.evt_set,
                                               ESP_AVRC_RN_PLAY_STATUS_CHANGE))
            register_notification(ESP_AVRC_RN_PLAY_STATUS_CHANGE);
        if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST,
                                               &param->get_rn_caps_rsp.evt_set,
                                               ESP_AVRC_RN_TRACK_CHANGE))
            register_notification(ESP_AVRC_RN_TRACK_CHANGE);
        if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST,
                                               &param->get_rn_caps_rsp.evt_set,
                                               ESP_AVRC_RN_PLAY_POS_CHANGED))
            register_notification(ESP_AVRC_RN_PLAY_POS_CHANGED, 1);
        break;
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
        if (param->change_ntf.event_id == ESP_AVRC_RN_PLAY_STATUS_CHANGE) {
            media_state().playback = param->change_ntf.event_parameter.playback;
            register_notification(ESP_AVRC_RN_PLAY_STATUS_CHANGE);
        } else if (param->change_ntf.event_id == ESP_AVRC_RN_TRACK_CHANGE) {
            ++media_state().revision;
            media_state().title.clear(); media_state().artist.clear(); media_state().album.clear();
            media_state().track.clear(); media_state().track_count.clear(); media_state().genre.clear();
            media_state().length_ms = media_state().position_ms = 0;
            request_metadata();
            register_notification(ESP_AVRC_RN_TRACK_CHANGE);
        } else if (param->change_ntf.event_id == ESP_AVRC_RN_PLAY_POS_CHANGED) {
            media_state().position_ms = param->change_ntf.event_parameter.play_pos;
            register_notification(ESP_AVRC_RN_PLAY_POS_CHANGED, 1);
        }
        send_media_state();
        break;
    default:
        break;
    }
}
#else
static void track_id(esp_avrc_rn_param_t &parameter) {
    memset(parameter.elm_id, 0, sizeof(parameter.elm_id));
    parameter.elm_id[0] = media_state().session >> 24;
    parameter.elm_id[1] = media_state().session >> 16;
    parameter.elm_id[2] = media_state().session >> 8;
    parameter.elm_id[3] = media_state().session;
    parameter.elm_id[4] = media_state().revision >> 24;
    parameter.elm_id[5] = media_state().revision >> 16;
    parameter.elm_id[6] = media_state().revision >> 8;
    parameter.elm_id[7] = media_state().revision;
}

static esp_avrc_rn_param_t notification_value(uint8_t event_id) {
    esp_avrc_rn_param_t parameter = {};
    if (event_id == ESP_AVRC_RN_PLAY_STATUS_CHANGE)
        parameter.playback = esp_avrc_playback_stat_t(media_state().playback);
    else if (event_id == ESP_AVRC_RN_TRACK_CHANGE)
        track_id(parameter);
    else if (event_id == ESP_AVRC_RN_PLAY_POS_CHANGED)
        parameter.play_pos = media_state().position_ms;
    return parameter;
}

static void notify_changed(uint8_t event_id) {
    if (!notification_registered[event_id]) return;
    auto parameter = notification_value(event_id);
    if (esp_avrc_tg_send_rn_rsp(esp_avrc_rn_event_ids_t(event_id), ESP_AVRC_RN_RSP_CHANGED,
                                &parameter) == ESP_OK)
        notification_registered[event_id] = false;
    else
        ++control_failures;
}

static void configure_target() {
    esp_avrc_psth_bit_mask_t commands = {};
    const esp_avrc_pt_cmd_t supported[] = {ESP_AVRC_PT_CMD_PLAY, ESP_AVRC_PT_CMD_PAUSE,
        ESP_AVRC_PT_CMD_STOP, ESP_AVRC_PT_CMD_FORWARD, ESP_AVRC_PT_CMD_BACKWARD,
        ESP_AVRC_PT_CMD_FAST_FORWARD, ESP_AVRC_PT_CMD_REWIND};
    for (auto command : supported)
        esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &commands, command);
    if (esp_avrc_tg_set_psth_cmd_filter(ESP_AVRC_PSTH_FILTER_SUPPORTED_CMD, &commands) != ESP_OK)
        ++control_failures;
    esp_avrc_rn_evt_cap_mask_t events = {};
    esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &events, ESP_AVRC_RN_PLAY_STATUS_CHANGE);
    esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &events, ESP_AVRC_RN_TRACK_CHANGE);
    esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &events, ESP_AVRC_RN_PLAY_POS_CHANGED);
    if (esp_avrc_tg_set_rn_evt_cap(&events) != ESP_OK)
        ++control_failures;
}

static void target_event(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param) {
    Guard guard;
    switch (event) {
    case ESP_AVRC_TG_PROF_STATE_EVT:
        if (param->avrc_tg_init_stat.state == ESP_AVRC_INIT_SUCCESS ||
            param->avrc_tg_init_stat.state == ESP_AVRC_INIT_ALREADY)
            configure_target();
        break;
    case ESP_AVRC_TG_CONNECTION_STATE_EVT:
        avrc_linked = param->conn_stat.connected;
        if (!avrc_linked) memset(notification_registered, 0, sizeof(notification_registered));
        ESP_LOGI(tag, "AVRCP Tesla control: %s", avrc_linked ? "connected" : "disconnected");
        break;
    case ESP_AVRC_TG_PASSTHROUGH_CMD_EVT:
        if (supported_control(param->psth_cmd.key_code)) {
            const dashlink::MusicCommand message{control_session, ++control_sequence,
                                                  param->psth_cmd.key_code,
                                                  param->psth_cmd.key_state};
            if (send_to_phone(message)) ++control_commands;
            else ++control_failures;
        }
        break;
    case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT: {
        const uint8_t id = param->reg_ntf.event_id;
        if (id == ESP_AVRC_RN_PLAY_STATUS_CHANGE || id == ESP_AVRC_RN_TRACK_CHANGE ||
            id == ESP_AVRC_RN_PLAY_POS_CHANGED) {
            auto parameter = notification_value(id);
            if (esp_avrc_tg_send_rn_rsp(esp_avrc_rn_event_ids_t(id), ESP_AVRC_RN_RSP_INTERIM,
                                        &parameter) == ESP_OK)
                notification_registered[id] = true;
            else
                ++control_failures;
        }
        break;
    }
    default:
        break;
    }
}
#endif

// Constrain both Bluetooth negotiations to one universally supported format.
// SBC frames can then cross the wire unchanged even though each board owns an
// independent A2DP connection.
static esp_a2d_mcc_t sbc_capability() {
    esp_a2d_mcc_t codec = {};
    codec.type = ESP_A2D_MCT_SBC;
    codec.cie.sbc_info.samp_freq = ESP_A2D_SBC_CIE_SF_44K;
    codec.cie.sbc_info.ch_mode = ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO;
    codec.cie.sbc_info.block_len = ESP_A2D_SBC_CIE_BLOCK_LEN_16;
    codec.cie.sbc_info.num_subbands = ESP_A2D_SBC_CIE_NUM_SUBBANDS_8;
    codec.cie.sbc_info.alloc_mthd = ESP_A2D_SBC_CIE_ALLOC_MTHD_LOUDNESS;
    codec.cie.sbc_info.min_bitpool = 2;
    codec.cie.sbc_info.max_bitpool = 53;
    return codec;
}

static bool compatible(const esp_a2d_mcc_t &codec) {
    const auto &sbc = codec.cie.sbc_info;
    return codec.type == ESP_A2D_MCT_SBC && sbc.samp_freq == ESP_A2D_SBC_CIE_SF_44K &&
           sbc.ch_mode == ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO &&
           sbc.block_len == ESP_A2D_SBC_CIE_BLOCK_LEN_16 &&
           sbc.num_subbands == ESP_A2D_SBC_CIE_NUM_SUBBANDS_8 &&
           sbc.alloc_mthd == ESP_A2D_SBC_CIE_ALLOC_MTHD_LOUDNESS && sbc.min_bitpool <= 53 &&
           sbc.max_bitpool >= 2;
}

static void clear_connection() {
    connecting = linked = streaming = codec_ready = start_pending = false;
    music_controller.set_music_streaming(false);
    connection = 0;
    connection_mtu = 0;
    stream = 0;
    sequence = 0;
}

static void a2dp_event(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param) {
    Guard guard;
    switch (event) {
    case ESP_A2D_PROF_STATE_EVT:
        profile_ready = param->a2d_prof_stat.init_state == ESP_A2D_INIT_SUCCESS;
        ESP_LOGI(tag, "A2DP profile: %s", profile_ready ? "ready" : "not ready");
        break;
    case ESP_A2D_SEP_REG_STATE_EVT:
        ESP_LOGI(tag, "A2DP SBC endpoint registration: state=%d", param->a2d_sep_reg_stat.reg_state);
        break;
    case ESP_A2D_CONNECTION_STATE_EVT:
        connecting = param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTING;
        if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
            finish_classic_profile(connections::Profile::music);
            linked = true;
            connecting = false;
            connection = param->conn_stat.conn_hdl;
            connection_mtu = param->conn_stat.audio_mtu;
            connected_at = now();
            stream = random_token();
            sequence = 0;
            ESP_LOGI(tag, "A2DP connected: handle=%u mtu=%u", unsigned(connection), unsigned(connection_mtu));
        } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            finish_classic_profile(connections::Profile::music);
            clear_connection();
            next_connect = now() + 5000;
            ESP_LOGW(tag, "A2DP disconnected; reconnect scheduled");
        }
        break;
    case ESP_A2D_AUDIO_CFG_EVT:
        codec_ready = compatible(param->audio_cfg.mcc);
        ESP_LOGI(tag, "A2DP codec: %s SBC 44.1 kHz joint stereo", codec_ready ? "compatible" : "incompatible");
        if (!codec_ready) {
#if CONFIG_BRIDGE_PHONE
            esp_a2d_sink_disconnect(param->audio_cfg.remote_bda);
#else
            esp_a2d_source_disconnect(param->audio_cfg.remote_bda);
#endif
        }
        break;
    case ESP_A2D_AUDIO_STATE_EVT:
        streaming = param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED;
        music_controller.set_music_streaming(streaming);
        if (streaming && !stream)
            stream = random_token();
        if (!streaming)
            sequence = 0;
        ESP_LOGI(tag, "A2DP stream: %s", streaming ? "started" : "suspended");
        break;
    case ESP_A2D_MEDIA_CTRL_ACK_EVT:
#if CONFIG_BRIDGE_CAR
        if (param->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_START) {
            start_pending = false;
            if (param->media_ctrl_stat.status != ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
                ++send_failures;
                ESP_LOGW(tag, "Tesla rejected A2DP stream start: status=%d", param->media_ctrl_stat.status);
            }
        }
#endif
        break;
    default:
        break;
    }
}

#if CONFIG_BRIDGE_PHONE
static void sink_audio(esp_a2d_conn_hdl_t handle, esp_a2d_audio_buff_t *buffer) {
    music::Audio audio;
    bool accepted = false;
    if (buffer && music_controller.allows_music_audio() && linked && streaming && codec_ready &&
        handle == connection && buffer->data &&
        buffer->data_len && buffer->data_len <= music::max_payload && buffer->number_frame) {
        audio.stream = stream;
        audio.sequence = ++sequence;
        audio.frames = buffer->number_frame;
        audio.timestamp = buffer->timestamp;
        audio.size = buffer->data_len;
        memcpy(audio.data.data(), buffer->data, audio.size);
        accepted = music_audio_transport_send(audio);
    }
    if (accepted)
        ++tx_packets;
    else
        ++dropped;
    if (buffer)
        esp_a2d_audio_buff_free(buffer);
}
#endif
} // namespace

void music_audio_transport_receive(const music::Audio &audio) {
#if CONFIG_BRIDGE_CAR
    Guard guard;
    ++rx_packets;
    if (!music_controller.allows_music_audio() || !linked || !codec_ready || !streaming || !connection || !audio.size ||
        audio.size > music::max_payload || (connection_mtu && audio.size > connection_mtu)) {
        ++dropped;
        return;
    }
    auto *buffer = esp_a2d_audio_buff_alloc(audio.size);
    if (!buffer) {
        ++dropped;
        ++send_failures;
        return;
    }
    buffer->number_frame = audio.frames;
    buffer->timestamp = audio.timestamp;
    buffer->data_len = audio.size;
    memcpy(buffer->data, audio.data.data(), audio.size);
    if (esp_a2d_source_audio_data_send(connection, buffer) != ESP_OK) {
        esp_a2d_audio_buff_free(buffer);
        ++dropped;
        ++send_failures;
    } else {
        ++tx_packets;
    }
#else
    (void)audio;
#endif
}

void music_peer_connected(const uint8_t *address) {
    if (!address)
        return;
    memcpy(peer, address, sizeof peer);
    peer_known = true;
    next_connect = now() + 500;
}

void music_peer_disconnected(const uint8_t *address) {
    if (address && peer_known && memcmp(peer, address, sizeof peer))
        return;
    if (linked) {
#if CONFIG_BRIDGE_PHONE
        esp_a2d_sink_disconnect(peer);
#else
        esp_a2d_source_disconnect(peer);
#endif
    }
    clear_connection();
    finish_classic_profile(connections::Profile::music);
    next_connect = now() + 5000;
}

void music_control_receive(const dashlink::Packet &packet) {
#if CONFIG_BRIDGE_PHONE
    const auto *message = std::get_if<dashlink::MusicCommand>(&packet);
    if (!message || !supported_control(message->key)) {
        ++control_failures;
        return;
    }
    if (!music_controller.accept_remote_command(message->session, message->sequence)) return;
    if (!avrc_linked || esp_avrc_ct_send_passthrough_cmd(next_transaction(), message->key, message->state) != ESP_OK)
        ++control_failures;
    else
        ++control_commands;
#else
    const auto *message = std::get_if<dashlink::MusicState>(&packet);
    if (!message) {
        ++control_failures;
        return;
    }
    const auto changes = music_controller.apply_remote_state(message->state);
    if (!changes.accepted) return;
    ++media_updates;
    if (changes.track) notify_changed(ESP_AVRC_RN_TRACK_CHANGE);
    if (changes.playback) notify_changed(ESP_AVRC_RN_PLAY_STATUS_CHANGE);
    if (changes.position) notify_changed(ESP_AVRC_RN_PLAY_POS_CHANGED);
#endif
}

void music_set_call_active(bool active) {
    music_controller.set_call_active(active);
}

bool music_audio_allowed() {
    Guard guard;
    return music_controller.allows_music_audio();
}

void music_poll() {
    if (profile_ready && peer_known && !linked && !connecting && now() >= next_connect &&
        try_begin_classic_profile(connections::Profile::music)) {
#if CONFIG_BRIDGE_PHONE
        const auto error = esp_a2d_sink_connect(peer);
#else
        const auto error = esp_a2d_source_connect(peer);
#endif
        connecting = error == ESP_OK;
        if (!connecting)
            finish_classic_profile(connections::Profile::music);
        next_connect = now() + (connecting ? 20000 : 5000);
        ESP_LOGI(tag, "A2DP connect request: %s", esp_err_to_name(error));
    }
    if (connecting && now() >= next_connect) {
        connecting = false;
        finish_classic_profile(connections::Profile::music);
        next_connect = now() + 5000;
        ESP_LOGW(tag, "A2DP connect timed out; retry scheduled");
    }
#if CONFIG_BRIDGE_CAR
    // The source must explicitly open its media channel. Retry only after the
    // profile and codec are settled; data remains truthfully dropped until the
    // Tesla acknowledges START.
    if (music_controller.allows_music_audio() && linked && codec_ready && !streaming &&
        !start_pending && now() - connected_at >= 500) {
        const auto error = esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
        start_pending = error == ESP_OK;
        connected_at = now();
        if (error != ESP_OK)
            ++send_failures;
    }
#endif
#if CONFIG_BRIDGE_PHONE
    if (avrc_linked && now() - last_media_send >= 2000)
        send_media_state();
#endif
}

void music_status() {
    ESP_LOGI(tag,
             "Music: profile=%d peer=%d connecting=%d linked=%d codec=%d streaming=%d avrc=%d focus=%s handle=%u mtu=%u tx=%lu rx=%lu dropped=%lu send_fail=%lu media=%lu controls=%lu control_fail=%lu",
             profile_ready, peer_known, connecting, linked, codec_ready, streaming, avrc_linked,
             music_controller.call_active() ? "call" : (streaming ? "music" : "idle"),
             unsigned(connection), unsigned(connection_mtu), (unsigned long)tx_packets, (unsigned long)rx_packets,
             (unsigned long)dropped, (unsigned long)send_failures, (unsigned long)media_updates,
             (unsigned long)control_commands, (unsigned long)control_failures);
}

void music_start() {
#if CONFIG_BRIDGE_PHONE
    music_controller.begin_local_session(random_token());
    ESP_ERROR_CHECK(esp_avrc_ct_register_callback(controller_event));
    ESP_ERROR_CHECK(esp_avrc_ct_init());
#else
    control_session = random_token();
    media_state().playback = ESP_AVRC_PLAYBACK_ERROR;
    ESP_ERROR_CHECK(esp_avrc_tg_register_callback(target_event));
    ESP_ERROR_CHECK(esp_avrc_tg_init());
#endif
    ESP_ERROR_CHECK(esp_a2d_register_callback(a2dp_event));
#if CONFIG_BRIDGE_PHONE
    ESP_ERROR_CHECK(esp_a2d_sink_init());
#else
    ESP_ERROR_CHECK(esp_a2d_source_init());
#endif
    const auto codec = sbc_capability();
#if CONFIG_BRIDGE_PHONE
    ESP_ERROR_CHECK(esp_a2d_sink_register_stream_endpoint(0, &codec));
    ESP_ERROR_CHECK(esp_a2d_sink_register_audio_data_callback(sink_audio));
#else
    ESP_ERROR_CHECK(esp_a2d_source_register_stream_endpoint(0, &codec));
#endif
    ESP_LOGI(tag, "Music initialized: SBC passthrough plus AVRCP controls and track metadata, UART %u",
             music::audio_baud);
}
} // namespace runtime

#if CONFIG_BRIDGE_CAR
extern "C" uint16_t dashbridge_avrc_metadata_value(uint32_t attribute, uint8_t *out,
                                                    uint16_t capacity) {
    runtime::Guard guard;
    const std::string *value = nullptr;
    std::string numeric;
    switch (attribute) {
    case 1: value = &runtime::media_state().title; break;
    case 2: value = &runtime::media_state().artist; break;
    case 3: value = &runtime::media_state().album; break;
    case 4: value = &runtime::media_state().track; break;
    case 5: value = &runtime::media_state().track_count; break;
    case 6: value = &runtime::media_state().genre; break;
    case 7:
        numeric = std::to_string((unsigned long)runtime::media_state().length_ms);
        value = &numeric;
        break;
    default:
        break;
    }
    if (!value || !out || !capacity) return 0;
    const uint16_t size = std::min<uint16_t>(capacity, value->size());
    memcpy(out, value->data(), size);
    return size;
}

extern "C" void dashbridge_avrc_play_status(uint32_t *length, uint32_t *position,
                                             uint8_t *playback) {
    runtime::Guard guard;
    *length = runtime::media_state().length_ms;
    *position = runtime::media_state().position_ms;
    *playback = runtime::media_state().playback;
}
#endif
#endif
