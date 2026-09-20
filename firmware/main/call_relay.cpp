#include "sdkconfig.h"
#if CONFIG_BRIDGE_CALL_RELAY
#include "runtime.hpp"
#include "call_relay.hpp"
#include "call_protocol.hpp"
#include <algorithm>
#include "esp_gap_bt_api.h"
#include "esp_bt_device.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#if CONFIG_BRIDGE_PHONE
#include "esp_hf_client_api.h"
#else
#include "esp_hf_ag_api.h"
#endif
#include <cstring>
namespace runtime {
using bridge::WireMessage;
using bridge::Op;
static calls::State self, other;
static calls::CommandGate commands;
static uint32_t boot, sequence = 0, other_boot = 0, other_sequence = 0, pending = 0;
static uint32_t pending_peer = 0;
static int64_t last_peer = 0, last_snapshot = 0, pending_deadline = 0, next_connect = 0, next_audio = 0;
static unsigned reconnect_delay = 5000;
static bool dirty = true, connecting = false, peer_saved = false;
static unsigned reconnect_attempts = 0;
static esp_err_t last_connect_result = ESP_OK;
static esp_bd_addr_t peer = {};
static std::string previous_snapshot, previous_number;
#if CONFIG_BRIDGE_PHONE
static bool command_fault = false;
static int64_t phone_ready_at = 0;
#endif
static uint32_t token() { uint32_t n; do { n = esp_random(); } while (!n); return n; }
static bool peer_live() { return other_boot && last_peer && now() - last_peer < 3000; }
bool relay_calls_ready() { return self.linked; }
static void transmit(const WireMessage &m) {
#if CONFIG_BRIDGE_PHONE
    send_to_car(m);
#else
    send_to_phone(m);
#endif
}
static WireMessage message(const char *kind) {
    WireMessage m{Op::call, boot, {}};
    m.notice.app = calls::protocol; m.notice.title = kind;
    return m;
}
static void snapshot() {
    auto m = message("state");
    m.notice.id = ++sequence; m.notice.body = calls::encode_state(self); m.notice.subtitle = self.number;
    transmit(m); last_snapshot = now(); dirty = false;
}
static bool known(const uint8_t *bda) {
    int count = esp_bt_gap_get_bond_device_num();
    esp_bd_addr_t bonds[16];
    if (count < 1 || count > 16 || esp_bt_gap_get_bond_device_list(&count, bonds) != ESP_OK) return false;
    for (int i = 0; i < count; ++i) if (!memcmp(bonds[i], bda, 6)) return true;
    return false;
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
    else if (changed) ESP_LOGI("calls", "Reconnect peer saved; bonded=%d", known(peer));
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
    const bool bonded = valid && known(peer);
    peer_saved = valid && bonded;
    ESP_LOGI("calls", "Reconnect diagnostics 1: stored=%d bonded=%d bonds=%d read=%s; auto reconnect=%s",
             valid, bonded, esp_bt_gap_get_bond_device_num(), esp_err_to_name(error), peer_saved ? "enabled" : "disabled");
}
static void set_audio(bool active) {
    if (active != bool(self.audio)) self.audio = active ? token() : 0;
    call_audio_set(self.audio, peer_live() ? other.audio : 0); dirty = true;
    ESP_LOGI("calls", "Local call audio: %s", active ? "connected (8 kHz)" : "disconnected");
}
static void set_connected(bool connected, const uint8_t *address) {
    connecting = false;
    if (connected) {
        save_peer(address); self.linked = 1; reconnect_delay = 5000;
    } else {
        self = {}; call_audio_set(0, 0);
        next_connect = now() + reconnect_delay;
        ESP_LOGI("calls", "Reconnect retry scheduled in %u ms", reconnect_delay);
        reconnect_delay = std::min(60000u, reconnect_delay * 2);
    }
    dirty = true;
    ESP_LOGI("calls", "Local phone profile: %s", connected ? "ready" : "disconnected");
}
#if CONFIG_BRIDGE_PHONE
static void reply(uint32_t id, uint32_t destination, bool ok) {
    auto m = message(ok ? "ok" : "error");
    m.notice.id = id; m.notice.date = std::to_string(destination);
    transmit(m);
}
static void phone_gap(esp_bt_gap_cb_event_t e, esp_bt_gap_cb_param_t *p) {
    Guard guard;
    if (e == ESP_BT_GAP_CFM_REQ_EVT)
        esp_bt_gap_ssp_confirm_reply(p->cfm_req.bda, pairing_allowed(Peer::phone) || known(p->cfm_req.bda));
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
        }
        else if (p->conn_stat.state == ESP_HF_CLIENT_CONNECTION_STATE_DISCONNECTED) {
            if (pending) reply(pending, pending_peer, false);
            pending = 0; set_connected(false, p->conn_stat.remote_bda);
        }
        break;
    case ESP_HF_CLIENT_AUDIO_STATE_EVT:
        if (p->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC) {
            ESP_LOGE("calls", "Unexpected wideband audio; refusing incompatible samples");
            esp_hf_client_disconnect_audio(peer);
        }
        set_audio(p->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED);
        break;
    case ESP_HF_CLIENT_CIND_CALL_EVT: self.call = p->call.status; dirty = true; break;
    case ESP_HF_CLIENT_CIND_CALL_SETUP_EVT: self.setup = p->call_setup.status; dirty = true; break;
    case ESP_HF_CLIENT_CIND_CALL_HELD_EVT: self.held = p->call_held.status; dirty = true; break;
    case ESP_HF_CLIENT_CIND_SERVICE_AVAILABILITY_EVT: self.service = p->service_availability.status; dirty = true; break;
    case ESP_HF_CLIENT_CIND_SIGNAL_STRENGTH_EVT: self.signal = p->signal_strength.value; dirty = true; break;
    case ESP_HF_CLIENT_CIND_ROAMING_STATUS_EVT: self.roam = p->roaming.status; dirty = true; break;
    case ESP_HF_CLIENT_CIND_BATTERY_LEVEL_EVT: self.battery = p->battery_level.value; dirty = true; break;
    case ESP_HF_CLIENT_CLIP_EVT:
        if (p->clip.number && calls::number_valid(p->clip.number)) self.number = p->clip.number;
        dirty = true; break;
    case ESP_HF_CLIENT_AT_RESPONSE_EVT:
        if (pending) {
            reply(pending, pending_peer, p->at_response.code == ESP_HF_AT_RESPONSE_CODE_OK);
            pending = 0;
        }
        break;
    default: break;
    }
}
static void receive_command(const WireMessage &m) {
    uint32_t destination = 0, generation = 0;
    if (!peer_live() || m.session != other_boot || !calls::decimal(m.notice.date, destination) || destination != boot)
        return;
    if (!commands.accept(m.session, m.notice.id)) return; // Never replay a control operation.
    if (pending || command_fault || !other.linked || now() - phone_ready_at < 1000 || !calls::decimal(m.notice.subtitle, generation) || generation != self.generation ||
        !calls::command_allowed(self, m.notice.title, m.notice.body)) {
        reply(m.notice.id, m.session, false); return;
    }
    esp_err_t error = ESP_ERR_NOT_SUPPORTED;
    if (m.notice.title == "dial") error = esp_hf_client_dial(m.notice.body.c_str());
    else if (m.notice.title == "answer") error = esp_hf_client_answer_call();
    else if (m.notice.title == "hangup") error = esp_hf_client_reject_call();
    else if (m.notice.title == "dtmf") error = esp_hf_client_send_dtmf(m.notice.body[0]);
    if (error == ESP_OK) {
        pending = m.notice.id; pending_peer = m.session; pending_deadline = now() + 4000;
    } else reply(m.notice.id, m.session, false);
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
    if (body == previous_snapshot && s.number == previous_number) return;
    previous_snapshot = body; previous_number = s.number;
    // IDF's answer_call API is its generic phone-state update entry point. It
    // generates ringing, call indicators and SCO transitions from real iPhone state.
    // The initial milestone maps one active/held call; multiparty control is rejected.
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
    m.notice.id = ++sequence; m.notice.body = argument;
    m.notice.date = std::to_string(other_boot); m.notice.subtitle = std::to_string(other.generation);
    pending = m.notice.id; pending_peer = other_boot; pending_deadline = now() + 5000;
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
            previous_snapshot.clear(); previous_number.clear();
            esp_hf_ag_bsir(peer, esp_hf_in_band_ring_state_t(0));
            apply_phone_state();
        } else if (p->conn_stat.state == ESP_HF_CONNECTION_STATE_DISCONNECTED) {
            pending = 0; set_connected(false, p->conn_stat.remote_bda);
        }
        break;
    case ESP_HF_AUDIO_STATE_EVT:
        if (p->audio_stat.state == ESP_HF_AUDIO_STATE_CONNECTED_MSBC) {
            ESP_LOGE("calls", "Unexpected wideband audio; refusing incompatible samples");
            esp_hf_ag_audio_disconnect(peer);
        }
        set_audio(p->audio_stat.state == ESP_HF_AUDIO_STATE_CONNECTED);
        break;
    case ESP_HF_CIND_RESPONSE_EVT:
        esp_hf_ag_cind_response(p->cind_rep.remote_addr, esp_hf_call_status_t(s.call), esp_hf_call_setup_status_t(s.setup),
                               esp_hf_network_state_t(s.service), s.signal, esp_hf_roaming_status_t(s.roam),
                               s.battery, esp_hf_call_held_status_t(s.held));
        break;
    case ESP_HF_IND_UPDATE_EVT: previous_snapshot.clear(); apply_phone_state(); break;
    case ESP_HF_COPS_RESPONSE_EVT:
        esp_hf_ag_cops_response(p->cops_rep.remote_addr, const_cast<char *>(s.linked ? "iPhone" : "No phone")); break;
    case ESP_HF_CLCC_RESPONSE_EVT:
        if (s.call || s.setup) {
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
        if (p->out_call.type == ESP_HF_DIAL_NUM && p->out_call.num_or_loc) {
            std::string number = p->out_call.num_or_loc;
            if (!number.empty() && number.back() == ';') number.pop_back();
            request("dial", number);
        } else result(false); // Explicitly reject redial and memory/VoIP dialing.
        break;
    case ESP_HF_BVRA_RESPONSE_EVT: result(false); break;
    case ESP_HF_UNAT_RESPONSE_EVT: esp_hf_ag_unknown_at_send(p->unat_rep.remote_addr, nullptr); break;
    default: break;
    }
}
#endif
void relay_receive(const WireMessage &m) {
    if (m.op != Op::call || m.notice.app != calls::protocol || !m.session) return;
    if (m.notice.title == "state") {
        calls::State decoded;
        if (!m.notice.id || !calls::decode_state(m.notice.body, m.notice.subtitle, decoded)) return;
        if (other_boot == m.session && m.notice.id <= other_sequence) return;
        if (other_boot != m.session) {
#if CONFIG_BRIDGE_CAR
            if (pending) result(false);
#endif
            // On A leave a command pending until its own AT result/timeout arrives;
            // do not let a rebooted B steal a late response from an earlier command.
#if CONFIG_BRIDGE_CAR
            pending = 0;
#endif
            other_boot = m.session; commands.reset(other_boot);
            ESP_LOGI("calls", "Other board detected (call relay protocol 1)");
        }
        other_sequence = m.notice.id; other = decoded; last_peer = now();
        call_audio_set(self.audio, other.audio);
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
    if (pending && m.session == pending_peer && m.notice.id == pending &&
        calls::decimal(m.notice.date, destination) && destination == boot &&
        (m.notice.title == "ok" || m.notice.title == "error")) {
        result(m.notice.title == "ok"); pending = 0;
    }
#endif
}
void relay_poll() {
#if CONFIG_BRIDGE_PHONE
    if (self.linked && (self.call || self.setup || self.held)) {
        if (!self.generation) { self.generation = token(); self.incoming = self.setup == 1; dirty = true; }
    } else if (self.generation) { self.generation = 0; self.number.clear(); self.incoming = 0; dirty = true; }
    if (self.linked && phone_notifications_ready()) paired(Peer::phone);
#endif
    if (last_peer && !peer_live()) {
        last_peer = 0; other = {}; call_audio_set(self.audio, 0);
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
        pending = 0;
        ESP_LOGW("calls", "Call command timed out; no automatic retry");
    }
    if (dirty || now() - last_snapshot >= 1000) snapshot();
    if (!self.linked && peer_saved && !connecting && now() >= next_connect) {
        ++reconnect_attempts;
        ESP_LOGI("calls", "Starting outgoing HFP reconnect attempt %u; bonded=%d", reconnect_attempts, known(peer));
#if CONFIG_BRIDGE_PHONE
        auto error = esp_hf_client_connect(peer);
#else
        auto error = esp_hf_ag_slc_connect(peer);
#endif
        last_connect_result = error;
        ESP_LOGI("calls", "Outgoing HFP reconnect request: %s; waiting up to 20 seconds", esp_err_to_name(error));
        connecting = error == ESP_OK; next_connect = now() + 20000;
    }
    if (connecting && now() >= next_connect) {
        ESP_LOGW("calls", "Outgoing HFP reconnect timed out; canceling, retry in %u ms", reconnect_delay);
#if CONFIG_BRIDGE_PHONE
        esp_hf_client_disconnect(peer);
#else
        esp_hf_ag_slc_disconnect(peer);
#endif
        connecting = false; next_connect = now() + reconnect_delay;
        reconnect_delay = std::min(60000u, reconnect_delay * 2);
    }
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
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, pairing_allowed(Peer::phone) ?
                                ESP_BT_GENERAL_DISCOVERABLE : ESP_BT_NON_DISCOVERABLE);
    }
#endif
}
void relay_status() {
    ESP_LOGI("calls", "Call profile: %s; other board: %s; one-call prototype", self.linked ? "ready" : "not ready",
             peer_live() ? (other.linked ? "ready" : "phone profile not ready") : "not connected");
    ESP_LOGI("calls", "Reconnect: saved=%d in_progress=%d attempts=%u last_request=%s retry_in_ms=%lld",
             peer_saved, connecting, reconnect_attempts, esp_err_to_name(last_connect_result),
             (long long)((!self.linked && peer_saved) ? std::max<int64_t>(0, next_connect - now()) : 0));
    call_audio_status();
}
void relay_start() {
    boot = token(); load_peer(); next_connect = now() + 5000;
    call_audio_start();
#if CONFIG_BRIDGE_PHONE
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(phone_gap));
    ESP_ERROR_CHECK(esp_bt_gap_set_device_name("DashBridge A"));
    esp_bt_io_cap_t capability = ESP_BT_IO_CAP_NONE;
    ESP_ERROR_CHECK(esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &capability, sizeof capability));
    esp_bt_cod_t cod = {}; cod.major = ESP_BT_COD_MAJOR_DEV_AV; cod.minor = 1;
    cod.service = ESP_BT_COD_SRVC_AUDIO | ESP_BT_COD_SRVC_RENDERING;
    ESP_ERROR_CHECK(esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_ALL));
    ESP_ERROR_CHECK(esp_hf_client_register_callback(phone_hfp));
    ESP_ERROR_CHECK(esp_hf_client_init());
    ESP_ERROR_CHECK(esp_hf_client_register_data_callback(call_audio_in, call_audio_out));
#else
    ESP_ERROR_CHECK(esp_hf_ag_register_callback(car_hfp));
    ESP_ERROR_CHECK(esp_hf_ag_init());
    ESP_ERROR_CHECK(esp_hf_ag_register_data_callback(call_audio_in, call_audio_out));
#endif
    ESP_LOGI("calls", "Two-board call alpha: UART1 audio TX25/RX26, UART2 control TX17/RX16; music unavailable");
}
}
#endif
