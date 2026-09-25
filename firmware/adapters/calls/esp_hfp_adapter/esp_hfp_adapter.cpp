#include "sdkconfig.h"
#if CONFIG_BRIDGE_CALL_RELAY
#include "dashbridge/adapters/features.hpp"
#include "dashbridge/adapters/call_audio_test.hpp"
#include "dashbridge/ports/runtime_services.hpp"
#include "dashbridge/protocols/calls_v3.hpp"
#include <algorithm>
#include "esp_gap_bt_api.h"
#include "esp_bt_device.h"
#include "esp_log.h"
#include "nvs.h"
#if CONFIG_BRIDGE_PHONE
#include "esp_hf_client_api.h"
#else
#include "esp_hf_ag_api.h"
#endif
#include <cstring>
namespace runtime {
using namespace dashbridge::ports;
namespace dashlink = dashbridge::protocols::dashlink_v2;
static calls::Controller call_state;
static calls::State &self = call_state.local();
static calls::State &other = call_state.remote();
static unsigned audio_rate = 0;
static calls::CommandGate &commands = call_state.commands();
static calls::CommandGate &test_commands = call_state.test_commands();
static uint32_t test_sequence = 0, test_pending = 0;
static int64_t test_deadline = 0;
static uint32_t boot, sequence = 0, pending = 0;
static uint32_t pending_peer = 0;
static int64_t last_peer = 0, last_snapshot = 0, pending_deadline = 0, next_audio = 0;
static std::string pending_command, pending_argument;
static bool dirty = true, peer_saved = false;
#if CONFIG_BRIDGE_PHONE
static int64_t next_connect = 0;
static unsigned reconnect_delay = 5000, reconnect_attempts = 0;
static bool connecting = false;
static esp_err_t last_connect_result = ESP_OK;
#endif
static esp_bd_addr_t peer = {};
static std::string last_call_snapshot, last_call_number;
static std::string peer_audio_counters;
static int64_t peer_audio_at = 0, last_audio_report = 0;
static uint16_t sync_handle = 0xffff;
static std::string radio_counters, peer_radio_counters;
static int64_t radio_at = 0, peer_radio_at = 0;
#if CONFIG_BRIDGE_PHONE
static bool command_fault = false;
static int64_t phone_ready_at = 0;
static bool clcc_dirty = false, clcc_pending = false;
static int64_t clcc_deadline = 0, next_clcc = 0;
static std::array<calls::CurrentCall, calls::max_current_calls> staged_calls{};
static unsigned staged_count = 0;
#endif
static bool peer_live() { return call_state.remote_boot() && last_peer && now() - last_peer < 3000; }
bool relay_calls_ready() { return self.linked; }
static void transmit(const dashlink::Call &message) {
#if CONFIG_BRIDGE_PHONE
    send_to_car(message);
#else
    send_to_phone(message);
#endif
}
static dashlink::Call message(const char *kind) {
    return {boot, 0, kind, {}, {}, {}};
}
static void apply_audio_test(calls::AudioTestMode mode) {
    bool ok = call_audio_test(mode);
    ESP_LOGI("audio", "Local audio test %s: %s", calls::audio_test_name(mode),
             ok ? "applied (maximum 60 seconds)" : "rejected; answer a call first");
}
static void send_audio_test(calls::AudioTestMode mode) {
    if (!peer_live()) {
        ESP_LOGW("audio", "Other board unavailable; test command not sent"); return;
    }
    auto m = message("audio-test");
    m.sequence = ++test_sequence;
    m.payload = calls::audio_test_name(mode);
    m.destination = std::to_string(other.audio);
    transmit(m); test_pending = m.sequence; test_deadline = now() + 3000;
    ESP_LOGI("audio", "Other board audio test %s requested; awaiting confirmation", m.payload.c_str());
}
void relay_audio_test(bool phone, calls::AudioTestMode mode) {
    // Only one side runs an isolation test at a time. No calls are placed or answered here.
#if CONFIG_BRIDGE_PHONE
    const bool local = phone;
#else
    const bool local = !phone;
#endif
    if (local) {
        send_audio_test(calls::AudioTestMode::normal);
        apply_audio_test(mode);
    } else {
        apply_audio_test(calls::AudioTestMode::normal);
        send_audio_test(mode);
    }
}
void relay_audio_test_stop() {
    apply_audio_test(calls::AudioTestMode::normal);
    send_audio_test(calls::AudioTestMode::normal);
}
template<class Stats> static void radio_snapshot(const Stats &s) {
    char text[240];
    snprintf(text, sizeof text, "rx_total=%lu rx_ok=%lu rx_err=%lu rx_none=%lu rx_lost=%lu tx_total=%lu tx_discarded=%lu",
             (unsigned long)s.rx_total, (unsigned long)s.rx_correct, (unsigned long)s.rx_err,
             (unsigned long)s.rx_none, (unsigned long)s.rx_lost,
             (unsigned long)s.tx_total, (unsigned long)s.tx_discarded);
    radio_counters = text; radio_at = now();
    auto report = message("radio"); report.payload = radio_counters; transmit(report);
}
static void snapshot() {
    auto m = message("state");
    m.sequence = ++sequence; m.payload = calls::encode_state(self);
    transmit(m); last_snapshot = now(); dirty = false;
}
static void save_peer(const uint8_t *address) {
    const bool changed = !peer_saved || memcmp(peer, address, 6);
    memcpy(peer, address, 6); peer_saved = true;
    nvs_handle_t handle;
    auto error = nvs_open("callrelay", NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = nvs_set_blob(handle, "peer", peer, sizeof peer);
        if (error == ESP_OK) error = nvs_commit(handle);
        nvs_close(handle);
    }
    if (error != ESP_OK) ESP_LOGE("calls", "Reconnect peer could not be saved: %s", esp_err_to_name(error));
    else if (changed) ESP_LOGI("calls", "Reconnect peer saved; bonded=%d", classic_bond_known(peer));
}
static void load_peer() {
    nvs_handle_t handle;
    auto error = nvs_open("callrelay", NVS_READONLY, &handle);
    size_t size = sizeof peer;
    if (error == ESP_OK) {
        error = nvs_get_blob(handle, "peer", peer, &size);
        nvs_close(handle);
    }
    const bool valid = error == ESP_OK && size == sizeof peer;
    const bool bonded = valid && classic_bond_known(peer);
    peer_saved = valid && bonded;
    ESP_LOGI("calls", "Saved peer: stored=%d bonded=%d bonds=%d read=%s",
             valid, bonded, esp_bt_gap_get_bond_device_num(), esp_err_to_name(error));
}
static void set_audio(unsigned rate) {
    const bool active = rate != 0;
    if (active && (!self.audio || rate != audio_rate)) { radio_counters.clear(); radio_at = 0; }
    if (active != bool(self.audio) || rate != audio_rate) self.audio = active ? random_token() : 0;
    audio_rate = rate;
    call_audio_set(self.audio, peer_live() ? other.audio : 0, audio_rate); dirty = true;
    ESP_LOGI("calls", "Local call audio: %s; %u Hz", rate == 16000 ? "mSBC HD" :
             (rate == 8000 ? "CVSD fallback" : "disconnected"), rate);
}
static void set_connected(bool connected, const uint8_t *address) {
#if CONFIG_BRIDGE_PHONE
    connecting = false;
#endif
    classic_base_link_changed(connected);
    if (connected) {
        save_peer(address); self.linked = 1;
#if CONFIG_BRIDGE_MUSIC_RELAY
        music_peer_connected(address);
#endif
#if CONFIG_BRIDGE_CONTACT_SYNC
        contacts_peer_connected(address);
#endif
#if CONFIG_BRIDGE_PHONE
        reconnect_delay = 5000;
#endif
    } else {
#if CONFIG_BRIDGE_MUSIC_RELAY
        music_peer_disconnected(address);
#endif
#if CONFIG_BRIDGE_CONTACT_SYNC
        contacts_peer_disconnected(address);
#endif
        self = {}; audio_rate = 0; call_audio_set(0, 0, 0);
#if CONFIG_BRIDGE_PHONE
        clcc_dirty = clcc_pending = false; staged_count = 0;
        pending_command.clear(); pending_argument.clear();
        next_connect = now() + reconnect_delay;
        ESP_LOGI("calls", "Reconnect retry scheduled in %u ms", reconnect_delay);
        reconnect_delay = std::min(60000u, reconnect_delay * 2);
#else
        // Tesla is the connection initiator for a paired phone. Outgoing AG
        // attempts are rejected by the car and can race its own auto-connect.
        ESP_LOGI("calls", "Waiting for Tesla to reconnect to the listening HFP gateway");
#endif
    }
    dirty = true;
    ESP_LOGI("calls", "Local phone profile: %s", connected ? "ready" : "disconnected");
}
#if CONFIG_BRIDGE_PHONE
static void reply(uint32_t id, uint32_t destination, bool ok) {
    auto m = message(ok ? "ok" : "error");
    m.sequence = id; m.destination = std::to_string(destination);
    transmit(m);
}
static void phone_gap(esp_bt_gap_cb_event_t e, esp_bt_gap_cb_param_t *p) {
    Guard guard;
    if (e == ESP_BT_GAP_CFM_REQ_EVT) {
        const bool bonded = classic_bond_known(p->cfm_req.bda);
        // AccessorySetupKit may bridge the Classic profiles as soon as its BLE
        // pairing is secure, before ANCS subscriptions finish. Ordinary Classic
        // discovery remains hidden until notification sharing is ready below.
        const bool staged_pairing = pairing_allowed(Peer::phone) && phone_bluetooth_ready();
        ESP_LOGI("calls", "Classic confirmation: bonded=%d pairing_open=%d bluetooth_ready=%d accepted=%d",
                 bonded, pairing_allowed(Peer::phone), phone_bluetooth_ready(), bonded || staged_pairing);
        esp_bt_gap_ssp_confirm_reply(p->cfm_req.bda, bonded || staged_pairing);
    }
    else if (e == ESP_BT_GAP_PIN_REQ_EVT) {
        esp_bt_pin_code_t pin = {};
        esp_bt_gap_pin_reply(p->pin_req.bda, false, 0, pin);
    } else if (e == ESP_BT_GAP_AUTH_CMPL_EVT)
        ESP_LOGI("calls", "iPhone Classic pairing status=%d", p->auth_cmpl.stat);
}
static void phone_hfp(esp_hf_client_cb_event_t e, esp_hf_client_cb_param_t *p) {
    Guard guard;
    switch (e) {
    case ESP_HF_CLIENT_CONNECTION_STATE_EVT:
        ESP_LOGI("calls", "iPhone HFP state=%d (0=disconnected, 1=connecting, 2=connected, 3=ready, 4=disconnecting); target=%d",
                 p->conn_stat.state, !memcmp(peer, p->conn_stat.remote_bda, 6));
        if (p->conn_stat.state == ESP_HF_CLIENT_CONNECTION_STATE_SLC_CONNECTED) {
            command_fault = false; phone_ready_at = now();
            set_connected(true, p->conn_stat.remote_bda);
            self.chld = p->conn_stat.chld_feat & 0x7f;
            clcc_dirty = true; next_clcc = now() + 200;
            ESP_LOGI("calls", "iPhone three-way features: 0x%02x", self.chld);
        }
        else if (p->conn_stat.state == ESP_HF_CLIENT_CONNECTION_STATE_DISCONNECTED) {
            if (pending) reply(pending, pending_peer, false);
            pending = 0; set_connected(false, p->conn_stat.remote_bda);
        }
        break;
    case ESP_HF_CLIENT_AUDIO_STATE_EVT:
        set_audio(p->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC ? 16000 :
                  (p->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED ? 8000 : 0));
        sync_handle = self.audio ? p->audio_stat.sync_conn_handle : 0xffff;
        call_audio_connection(sync_handle);
        if (self.audio) ESP_LOGI("calls", "SCO handle=%u preferred_frame=%u", sync_handle, p->audio_stat.preferred_frame_size);
        break;
    case ESP_HF_CLIENT_PKT_STAT_NUMS_GET_EVT: radio_snapshot(p->pkt_nums); break;
    case ESP_HF_CLIENT_CIND_CALL_EVT: self.call = p->call.status; dirty = clcc_dirty = true; break;
    case ESP_HF_CLIENT_CIND_CALL_SETUP_EVT: self.setup = p->call_setup.status; dirty = clcc_dirty = true; break;
    case ESP_HF_CLIENT_CIND_CALL_HELD_EVT: self.held = p->call_held.status; dirty = clcc_dirty = true; break;
    case ESP_HF_CLIENT_CIND_SERVICE_AVAILABILITY_EVT: self.service = p->service_availability.status; dirty = true; break;
    case ESP_HF_CLIENT_CIND_SIGNAL_STRENGTH_EVT: self.signal = p->signal_strength.value; dirty = true; break;
    case ESP_HF_CLIENT_CIND_ROAMING_STATUS_EVT: self.roam = p->roaming.status; dirty = true; break;
    case ESP_HF_CLIENT_CIND_BATTERY_LEVEL_EVT: self.battery = p->battery_level.value; dirty = true; break;
    case ESP_HF_CLIENT_CLIP_EVT:
        if (p->clip.number && calls::number_valid(p->clip.number)) self.number = p->clip.number;
        dirty = true; break;
    case ESP_HF_CLIENT_CCWA_EVT:
        if (p->ccwa.number && calls::number_valid(p->ccwa.number)) self.number = p->ccwa.number;
        clcc_dirty = dirty = true;
        break;
    case ESP_HF_CLIENT_CLCC_EVT:
        if (clcc_pending && staged_count < staged_calls.size() && p->clcc.idx > 0 && p->clcc.idx <= 255 &&
            unsigned(p->clcc.dir) <= 1 && unsigned(p->clcc.status) <= 5 && unsigned(p->clcc.mpty) <= 1 &&
            (!p->clcc.number || calls::number_valid(p->clcc.number))) {
            staged_calls[staged_count++] = {unsigned(p->clcc.idx), unsigned(p->clcc.dir),
                unsigned(p->clcc.status), unsigned(p->clcc.mpty), p->clcc.number ? p->clcc.number : ""};
        }
        break;
    case ESP_HF_CLIENT_AT_RESPONSE_EVT:
        if (clcc_pending) {
            clcc_pending = false;
            if (p->at_response.code == ESP_HF_AT_RESPONSE_CODE_OK) {
                self.current_count = staged_count;
                self.current = staged_calls;
                if (self.current_count && !self.current[0].number.empty()) self.number = self.current[0].number;
                dirty = true;
            } else {
                clcc_dirty = true; next_clcc = now() + 2000;
            }
        } else if (pending) {
            const bool ok = p->at_response.code == ESP_HF_AT_RESPONSE_CODE_OK;
            reply(pending, pending_peer, ok);
            if (ok) { clcc_dirty = true; next_clcc = now() + 100; }
            pending = 0; pending_command.clear(); pending_argument.clear();
        }
        break;
    default: break;
    }
}
static void receive_command(const dashlink::Call &m) {
    uint32_t destination = 0, generation = 0;
    if (!peer_live() || m.session != call_state.remote_boot() ||
        !calls::decimal(m.destination, destination) || destination != boot)
        return;
    if (!commands.accept(m.session, m.sequence)) return; // Never replay a control operation.
    if (pending || clcc_pending || command_fault || !other.linked || now() - phone_ready_at < 1000 || !calls::decimal(m.auxiliary, generation) || generation != self.generation ||
        !calls::command_allowed(self, m.kind, m.payload)) {
        reply(m.sequence, m.session, false); return;
    }
    esp_err_t error = ESP_ERR_NOT_SUPPORTED;
    if (m.kind == "dial") error = esp_hf_client_dial(m.payload.c_str());
    else if (m.kind == "redial") error = esp_hf_client_dial(nullptr);
    else if (m.kind == "answer") error = esp_hf_client_answer_call();
    else if (m.kind == "hangup") error = esp_hf_client_reject_call();
    else if (m.kind == "dtmf") error = esp_hf_client_send_dtmf(m.payload[0]);
    else if (m.kind == "chld") {
        const auto &a = m.payload;
        esp_hf_chld_type_t type = ESP_HF_CHLD_TYPE_REL;
        int index = 0;
        if (a == "0") type = ESP_HF_CHLD_TYPE_REL;
        else if (a == "1") type = ESP_HF_CHLD_TYPE_REL_ACC;
        else if (a == "2") type = ESP_HF_CHLD_TYPE_HOLD_ACC;
        else if (a == "3") type = ESP_HF_CHLD_TYPE_MERGE;
        else if (a == "4") type = ESP_HF_CHLD_TYPE_MERGE_DETACH;
        else {
            uint32_t parsed = 0;
            calls::decimal(a.substr(1), parsed); index = parsed;
            type = a[0] == '1' ? ESP_HF_CHLD_TYPE_REL_X : ESP_HF_CHLD_TYPE_PRIV_X;
        }
        error = esp_hf_client_send_chld_cmd(type, index);
    }
    if (error == ESP_OK) {
        pending = m.sequence; pending_peer = m.session; pending_deadline = now() + 4000;
        pending_command = m.kind; pending_argument = m.payload;
    } else reply(m.sequence, m.session, false);
}
#else
static void result(bool ok) {
    if (self.linked) esp_hf_ag_cmee_send(peer, ok ? ESP_HF_AT_RESPONSE_CODE_OK : ESP_HF_AT_RESPONSE_CODE_CME,
                                       ESP_HF_CME_OPERATION_NOT_SUPPORTED);
}
static calls::State effective() { return peer_live() && other.linked ? other : calls::State{}; }
static void apply_phone_state() {
    if (!self.linked) return;
    auto s = effective();
    auto body = calls::encode_state(s);
    if (body == last_call_snapshot && s.number == last_call_number) return;
    last_call_snapshot = body; last_call_number = s.number;
    // IDF's answer_call API is its generic phone-state update entry point. It
    // generates ringing, call indicators and SCO transitions from real iPhone state.
    // The indicator update drives ringing/SCO. Detailed call identities are
    // answered separately from the iPhone's current-call list.
    int held = s.held ? 1 : 0;
    int active = s.call && s.held != 2 ? 1 : 0;
    if (!s.call && !s.setup && !s.held)
        esp_hf_ag_end_call(peer, 0, 0, ESP_HF_CALL_STATUS_NO_CALLS, ESP_HF_CALL_SETUP_STATUS_IDLE,
                           const_cast<char *>(""), ESP_HF_CALL_ADDR_TYPE_UNKNOWN);
    else
        esp_hf_ag_answer_call(peer, active, held, esp_hf_call_status_t(s.call), esp_hf_call_setup_status_t(s.setup),
                              const_cast<char *>(s.number.c_str()), s.number.size() && s.number[0] == '+' ?
                              ESP_HF_CALL_ADDR_TYPE_INTERNATIONAL : ESP_HF_CALL_ADDR_TYPE_UNKNOWN);
    esp_hf_ag_ciev_report(peer, ESP_HF_IND_TYPE_SERVICE, s.service);
    esp_hf_ag_ciev_report(peer, ESP_HF_IND_TYPE_SIGNAL, s.signal);
    esp_hf_ag_ciev_report(peer, ESP_HF_IND_TYPE_ROAM, s.roam);
    esp_hf_ag_ciev_report(peer, ESP_HF_IND_TYPE_BATTCHG, s.battery);
}
static void request(const char *command, const std::string &argument = {}) {
    if (pending || !calls::command_allowed(effective(), command, argument)) { result(false); return; }
    auto m = message(command);
    m.sequence = ++sequence; m.payload = argument;
    m.destination = std::to_string(call_state.remote_boot()); m.auxiliary = std::to_string(other.generation);
    pending = m.sequence; pending_peer = call_state.remote_boot(); pending_deadline = now() + 5000;
    transmit(m);
}
static void car_hfp(esp_hf_cb_event_t e, esp_hf_cb_param_t *p) {
    Guard guard;
    auto s = effective();
    switch (e) {
    case ESP_HF_CONNECTION_STATE_EVT:
        ESP_LOGI("calls", "Tesla HFP state=%d (0=disconnected, 1=connecting, 2=connected, 3=ready, 4=disconnecting); target=%d",
                 p->conn_stat.state, !memcmp(peer, p->conn_stat.remote_bda, 6));
        if (p->conn_stat.state == ESP_HF_CONNECTION_STATE_SLC_CONNECTED) {
            set_connected(true, p->conn_stat.remote_bda);
            last_call_snapshot.clear(); last_call_number.clear();
            esp_hf_ag_bsir(peer, esp_hf_in_band_ring_state_t(0));
            apply_phone_state();
        } else if (p->conn_stat.state == ESP_HF_CONNECTION_STATE_DISCONNECTED) {
            pending = 0; set_connected(false, p->conn_stat.remote_bda);
        }
        break;
    case ESP_HF_AUDIO_STATE_EVT:
        set_audio(p->audio_stat.state == ESP_HF_AUDIO_STATE_CONNECTED_MSBC ? 16000 :
                  (p->audio_stat.state == ESP_HF_AUDIO_STATE_CONNECTED ? 8000 : 0));
        sync_handle = self.audio ? p->audio_stat.sync_conn_handle : 0xffff;
        call_audio_connection(sync_handle);
        if (self.audio) ESP_LOGI("calls", "SCO handle=%u preferred_frame=%u", sync_handle, p->audio_stat.preferred_frame_size);
        break;
    case ESP_HF_PKT_STAT_NUMS_GET_EVT: radio_snapshot(p->pkt_nums); break;
    case ESP_HF_CIND_RESPONSE_EVT:
        esp_hf_ag_cind_response(p->cind_rep.remote_addr, esp_hf_call_status_t(s.call), esp_hf_call_setup_status_t(s.setup),
                               esp_hf_network_state_t(s.service), s.signal, esp_hf_roaming_status_t(s.roam),
                               s.battery, esp_hf_call_held_status_t(s.held));
        break;
    case ESP_HF_IND_UPDATE_EVT: last_call_snapshot.clear(); apply_phone_state(); break;
    case ESP_HF_COPS_RESPONSE_EVT:
        esp_hf_ag_cops_response(p->cops_rep.remote_addr, const_cast<char *>(s.linked ? "iPhone" : "No phone")); break;
    case ESP_HF_CLCC_RESPONSE_EVT:
        if (s.current_count) {
            for (size_t i = 0; i < s.current_count; ++i) {
                const auto &call = s.current[i];
                esp_hf_ag_clcc_response(p->clcc_rep.remote_addr, call.index,
                    esp_hf_current_call_direction_t(call.direction), esp_hf_current_call_status_t(call.status),
                    ESP_HF_CURRENT_CALL_MODE_VOICE, esp_hf_current_call_mpty_type_t(call.multiparty),
                    call.number.empty() ? nullptr : const_cast<char *>(call.number.c_str()),
                    call.number.size() && call.number[0] == '+' ? ESP_HF_CALL_ADDR_TYPE_INTERNATIONAL :
                    ESP_HF_CALL_ADDR_TYPE_UNKNOWN);
            }
        } else if (s.call || s.setup) {
            unsigned status = s.held == 2 ? 1 : (s.call ? 0 : (s.setup == 1 ? 4 : s.setup));
            esp_hf_ag_clcc_response(p->clcc_rep.remote_addr, 1, esp_hf_current_call_direction_t(s.incoming),
                esp_hf_current_call_status_t(status), ESP_HF_CURRENT_CALL_MODE_VOICE,
                ESP_HF_CURRENT_CALL_MPTY_TYPE_SINGLE, s.number.empty() ? nullptr : const_cast<char *>(s.number.c_str()),
                s.number.size() && s.number[0] == '+' ? ESP_HF_CALL_ADDR_TYPE_INTERNATIONAL : ESP_HF_CALL_ADDR_TYPE_UNKNOWN);
        }
        esp_hf_ag_clcc_response(p->clcc_rep.remote_addr, 0, ESP_HF_CURRENT_CALL_DIRECTION_OUTGOING,
            ESP_HF_CURRENT_CALL_STATUS_ACTIVE, ESP_HF_CURRENT_CALL_MODE_VOICE, ESP_HF_CURRENT_CALL_MPTY_TYPE_SINGLE,
            nullptr, ESP_HF_CALL_ADDR_TYPE_UNKNOWN); break;
    case ESP_HF_CNUM_RESPONSE_EVT:
        esp_hf_ag_cnum_response(p->cnum_rep.remote_addr, const_cast<char *>(""), 129,
                               ESP_HF_SUBSCRIBER_SERVICE_TYPE_UNKNOWN); break;
    case ESP_HF_ATA_RESPONSE_EVT: request("answer"); break;
    case ESP_HF_CHUP_RESPONSE_EVT: request("hangup"); break;
    case ESP_HF_VTS_RESPONSE_EVT: request("dtmf", p->vts_rep.code ? p->vts_rep.code : ""); break;
    case ESP_HF_DIAL_EVT:
        if (p->out_call.type == ESP_HF_DIAL_NUM) {
            std::string number = p->out_call.num_or_loc ? p->out_call.num_or_loc : "";
            if (!number.empty() && number.back() == ';') number.pop_back();
            if (number.empty()) request("redial");
            else request("dial", number);
        } else result(false); // Explicitly reject memory/VoIP dialing.
        break;
    case ESP_HF_BVRA_RESPONSE_EVT: result(false); break;
    case ESP_HF_UNAT_RESPONSE_EVT: {
        std::string argument;
        if (p->unat_rep.unat && calls::chld_argument(p->unat_rep.unat, argument)) request("chld", argument);
        else esp_hf_ag_unknown_at_send(p->unat_rep.remote_addr, nullptr);
        break;
    }
    default: break;
    }
}
#endif
void relay_receive(const dashlink::Call &m) {
    if (!m.session) return;
    if (m.kind == "audio-test" && peer_live() && m.session == call_state.remote_boot()) {
        if (!test_commands.accept(m.session, m.sequence)) return;
        calls::AudioTestMode mode;
        if (m.payload == "normal") mode = calls::AudioTestMode::normal;
        else if (m.payload == "tone") mode = calls::AudioTestMode::tone;
        else if (m.payload == "loopback") mode = calls::AudioTestMode::loopback;
        else return;
        uint32_t requested_audio = 0;
        bool current = calls::decimal(m.destination, requested_audio) &&
            (mode == calls::AudioTestMode::normal || (self.audio && requested_audio == self.audio));
        bool ok = current && call_audio_test(mode);
        auto reply = message("audio-test-result");
        reply.sequence = m.sequence; reply.destination = std::to_string(m.session);
        reply.payload = std::string(calls::audio_test_name(mode)) + (ok ? " applied" : " rejected; answer a call first");
        transmit(reply);
        ESP_LOGI("audio", "Remote audio test: %s", reply.payload.c_str());
        return;
    }
    if (m.kind == "audio-test-result" && peer_live() && m.session == call_state.remote_boot()) {
        uint32_t destination = 0;
        if (test_pending && m.sequence == test_pending && calls::decimal(m.destination, destination) &&
            destination == boot && m.payload.size() < 100) {
            ESP_LOGI("audio", "Other board audio test: %s", m.payload.c_str());
            test_pending = 0;
        }
        return;
    }
    if ((m.kind == "audio" || m.kind == "radio") && peer_live() &&
        m.session == call_state.remote_boot()) {
        if (m.payload.size() < 512) {
            if (m.kind == "audio") { peer_audio_counters = m.payload; peer_audio_at = now(); }
            else { peer_radio_counters = m.payload; peer_radio_at = now(); }
        }
        return;
    }
    if (m.kind == "state") {
        calls::State decoded;
        if (!m.sequence || !calls::decode_state(m.payload, m.auxiliary, decoded)) return;
        const auto snapshot = call_state.apply_remote_snapshot(m.session, m.sequence, std::move(decoded));
        if (snapshot == calls::SnapshotResult::rejected) return;
        if (snapshot == calls::SnapshotResult::new_session) {
#if CONFIG_BRIDGE_CAR
            if (pending) result(false);
#endif
            // On A leave a command pending until its own AT result/timeout arrives;
            // do not let a rebooted B accept a late response from a stale command.
#if CONFIG_BRIDGE_CAR
            pending = 0;
#endif
            test_pending = 0; peer_audio_counters.clear(); peer_audio_at = 0;
            peer_radio_counters.clear(); peer_radio_at = 0;
            ESP_LOGI("calls", "Other board detected (call relay protocol 3; HD audio and conferences supported)");
        }
        last_peer = now();
        call_audio_set(self.audio, other.audio, audio_rate);
#if CONFIG_BRIDGE_PHONE
        if (!other.linked && self.audio) esp_hf_client_disconnect_audio(peer);
#endif
#if CONFIG_BRIDGE_CAR
        apply_phone_state();
#endif
        return;
    }
#if CONFIG_BRIDGE_PHONE
    receive_command(m);
#else
    uint32_t destination = 0;
    if (pending && m.session == pending_peer && m.sequence == pending &&
        calls::decimal(m.destination, destination) && destination == boot &&
        (m.kind == "ok" || m.kind == "error")) {
        result(m.kind == "ok"); pending = 0;
    }
#endif
}
void relay_poll() {
#if CONFIG_BRIDGE_MUSIC_RELAY
    music_set_call_active(self.call || self.setup || self.held || self.audio ||
                          other.call || other.setup || other.held || other.audio);
    music_poll();
#endif
#if CONFIG_BRIDGE_CONTACT_SYNC
    contacts_poll();
#endif
    if (test_pending && now() >= test_deadline) {
        ESP_LOGW("audio", "Other board did not confirm audio test; check its firmware and status");
        test_pending = 0;
    }
#if CONFIG_BRIDGE_PHONE
    if (self.linked && (self.call || self.setup || self.held)) {
        if (!self.generation) { self.generation = random_token(); self.incoming = self.setup == 1; dirty = true; }
    } else if (self.generation) {
        self.generation = 0; self.number.clear(); self.incoming = 0; self.current_count = 0; dirty = true;
    }
    if (self.linked && clcc_dirty && !clcc_pending && !pending && now() >= next_clcc) {
        staged_count = 0;
        const auto error = esp_hf_client_query_current_calls();
        if (error == ESP_OK) {
            clcc_pending = true; clcc_dirty = false; clcc_deadline = now() + 4000;
        } else {
            next_clcc = now() + 2000;
        }
    }
    if (clcc_pending && now() >= clcc_deadline) {
        clcc_pending = false; command_fault = true;
        esp_hf_client_disconnect(peer);
        ESP_LOGW("calls", "Current-call query timed out; resetting HFP to prevent a late response");
    }
    if (self.linked && phone_notifications_ready()) paired(Peer::phone);
#endif
    if (last_peer && !peer_live()) {
        last_peer = 0; call_state.clear_remote_state(); call_audio_set(self.audio, 0, audio_rate);
        ESP_LOGW("calls", "Other board heartbeat lost; audio cleared");
#if CONFIG_BRIDGE_PHONE
        if (self.audio) esp_hf_client_disconnect_audio(peer); // Return audio routing to the iPhone; never hang up.
#else
        if (pending) { result(false); pending = 0; }
        apply_phone_state();
#endif
    }
    if (pending && now() >= pending_deadline) {
#if CONFIG_BRIDGE_PHONE
        reply(pending, pending_peer, false);
        // AT responses have no request ID. Reset the SLC after timeout so a late
        // result cannot acknowledge the next command. Never retry call actions.
        command_fault = true;
        esp_hf_client_disconnect(peer);
#else
        result(false);
#endif
        pending = 0; pending_command.clear(); pending_argument.clear();
        ESP_LOGW("calls", "Call command timed out; no automatic retry");
    }
    if (dirty || now() - last_snapshot >= 1000) snapshot();
    if (now() - last_audio_report >= 5000) {
        auto diagnostics = message("audio");
        diagnostics.payload = call_audio_diagnostics();
        transmit(diagnostics); last_audio_report = now();
        if (self.audio && sync_handle != 0xffff) {
#if CONFIG_BRIDGE_PHONE
            esp_hf_client_pkt_stat_nums_get(sync_handle);
#else
            esp_hf_ag_pkt_stat_nums_get(sync_handle);
#endif
        }
    }
    // Reconnect policy: only the iPhone-facing HFP client initiates.
#if CONFIG_BRIDGE_PHONE
    // The iPhone-facing HFP client owns its outgoing reconnect policy. Board B
    // is an HFP gateway and stays passive so Tesla can initiate its normal
    // HFP + MAP auto-connect sequence.
    if (!self.linked && peer_saved && !connecting && now() >= next_connect) {
        ++reconnect_attempts;
        ESP_LOGI("calls", "Starting outgoing HFP reconnect attempt %u; bonded=%d",
                 reconnect_attempts, classic_bond_known(peer));
        auto error = esp_hf_client_connect(peer);
        last_connect_result = error;
        ESP_LOGI("calls", "Outgoing HFP reconnect request: %s; waiting up to 20 seconds", esp_err_to_name(error));
        connecting = error == ESP_OK; next_connect = now() + 20000;
    }
    if (connecting && now() >= next_connect) {
        ESP_LOGW("calls", "Outgoing HFP reconnect timed out; canceling, retry in %u ms", reconnect_delay);
        esp_hf_client_disconnect(peer);
        connecting = false; next_connect = now() + reconnect_delay;
        reconnect_delay = std::min(60000u, reconnect_delay * 2);
    }
#endif
    // Audio connection follows the real call state on both boards.
    if (peer_live() && self.linked && other.linked && other.audio && !self.audio && now() >= next_audio) {
        next_audio = now() + 5000;
#if CONFIG_BRIDGE_PHONE
        if (self.call || self.setup) esp_hf_client_connect_audio(peer);
#else
        if (other.call || other.setup) esp_hf_ag_audio_connect(peer);
#endif
    }
#if CONFIG_BRIDGE_PHONE
    static int64_t last_scan = 0;
    if (now() - last_scan >= 1000) {
        last_scan = now();
        const bool staged_pairing = pairing_allowed(Peer::phone) && phone_notifications_ready();
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, staged_pairing ?
                                ESP_BT_GENERAL_DISCOVERABLE : ESP_BT_NON_DISCOVERABLE);
    }
#endif
}
void relay_status() {
#if CONFIG_BRIDGE_PHONE
    const auto &conference = self;
#else
    const auto &conference = other;
#endif
    ESP_LOGI("calls", "Call profile: %s; other board: %s; multiparty=%s calls=%u", self.linked ? "ready" : "not ready",
             peer_live() ? (other.linked ? "ready" : "phone profile not ready") : "not connected",
             (conference.chld & 0x20) ? "supported" : "unavailable", conference.current_count);
#if CONFIG_BRIDGE_PHONE
    ESP_LOGI("calls", "Reconnect: saved=%d in_progress=%d attempts=%u last_request=%s retry_in_ms=%lld",
             peer_saved, connecting, reconnect_attempts, esp_err_to_name(last_connect_result),
             (long long)((!self.linked && peer_saved) ? std::max<int64_t>(0, next_connect - now()) : 0));
#else
    ESP_LOGI("calls", "Reconnect: passive HFP gateway; saved=%d listening_for_tesla=%d",
             peer_saved, !self.linked);
#endif
    call_audio_status();
#if CONFIG_BRIDGE_MUSIC_RELAY
    music_status();
#endif
#if CONFIG_BRIDGE_CONTACT_SYNC
    contacts_status();
#endif
    if (peer_live() && peer_audio_at && now() - peer_audio_at < 10000)
        ESP_LOGI("calls", "Other board audio counters: %s", peer_audio_counters.c_str());
    if (radio_at) ESP_LOGI("calls", "Local Bluetooth audio counters: %s; age_ms=%lld",
                          radio_counters.c_str(), (long long)(now() - radio_at));
    if (peer_live() && peer_radio_at) ESP_LOGI("calls", "Other board Bluetooth audio counters: %s; age_ms=%lld",
                                            peer_radio_counters.c_str(), (long long)(now() - peer_radio_at));
}
void relay_start() {
    boot = random_token(); load_peer();
#if CONFIG_BRIDGE_PHONE
    next_connect = now() + 5000;
#else
    ESP_LOGI("calls", "Tesla reconnect policy: passive HFP gateway with MAP enabled");
#endif
    call_audio_start();
#if CONFIG_BRIDGE_PHONE
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(phone_gap));
    ESP_ERROR_CHECK(esp_bt_gap_set_device_name("Dash Calls"));
    // Initial setup is staged: the app establishes BLE/ANCS first. Dash Calls
    // becomes discoverable only after notification sharing is ready.
    ESP_ERROR_CHECK(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE));
    esp_bt_io_cap_t capability = ESP_BT_IO_CAP_NONE;
    ESP_ERROR_CHECK(esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &capability, sizeof capability));
    esp_bt_cod_t cod = {}; cod.major = ESP_BT_COD_MAJOR_DEV_AV; cod.minor = 1;
    cod.service = ESP_BT_COD_SRVC_AUDIO | ESP_BT_COD_SRVC_RENDERING;
    ESP_ERROR_CHECK(esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_ALL));
    ESP_ERROR_CHECK(esp_hf_client_register_callback(phone_hfp));
    ESP_ERROR_CHECK(esp_hf_client_init());
#if CONFIG_BT_HFP_USE_EXTERNAL_CODEC
    call_audio_register();
#else
    ESP_ERROR_CHECK(esp_hf_client_register_data_callback(call_audio_in, call_audio_out));
#endif
#else
    ESP_ERROR_CHECK(esp_hf_ag_register_callback(car_hfp));
    ESP_ERROR_CHECK(esp_hf_ag_init());
#if CONFIG_BT_HFP_USE_EXTERNAL_CODEC
    call_audio_register();
#else
    ESP_ERROR_CHECK(esp_hf_ag_register_data_callback(call_audio_in, call_audio_out));
#endif
#endif
#if CONFIG_BRIDGE_MUSIC_RELAY
    music_start();
#endif
#if CONFIG_BRIDGE_CONTACT_SYNC
    contacts_start();
#endif
    ESP_LOGI("calls", "Two-board relay: UART1 call/music audio TX25/RX26, UART2 control TX17/RX16");
}
}
#endif
