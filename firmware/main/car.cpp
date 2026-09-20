#include "runtime.hpp"
#include "sdkconfig.h"
#if CONFIG_BRIDGE_CAR
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_hf_ag_api.h"
#include "esp_log.h"
#include "esp_sdp_api.h"
#include "esp_spp_api.h"
#include <algorithm>
#include <cstring>
#include <deque>
namespace runtime {
using namespace bridge;
static const char *tag = "car";
static Inbox inbox;
static MasServer mas(inbox);
struct Channel {
    uint32_t handle = 0;
    bool writing = false, congested = false;
    std::deque<Bytes> queue;
    ObexFramer framer;
    void clear() {
        handle = 0;
        writing = false;
        congested = false;
        queue.clear();
        framer.clear();
    }
    void pump() {
        if (handle && !writing && !congested && !queue.empty()) {
            auto &b = queue.front();
            auto err = esp_spp_write(handle, b.size(), b.data());
            if (err == ESP_OK)
                writing = true;
            else {
                ESP_LOGE(tag, "SPP write: %s", esp_err_to_name(err));
                esp_spp_disconnect(handle);
            }
        }
    }
    void send(Bytes b) {
        if (queue.size() >= 16) {
            ESP_LOGE(tag, "SPP queue overflow; disconnecting");
            esp_spp_disconnect(handle);
            return;
        }
        queue.push_back(std::move(b));
        pump();
    }
    void written(bool congest) {
        if (writing && !queue.empty())
            queue.pop_front();
        writing = false;
        congested = congest;
        pump();
    }
};
static Channel server, events;
static esp_bd_addr_t peer = {};
static bool sdp_ready = false, record_created = false, discovering = false;
static uint8_t channel_number = 0;
static int mns_state = 0; // 0 idle, 1 discovery/connect, 2 OBEX connect, 3 ready, 4 event awaiting response
static uint32_t mns_id = 0;
static int64_t mns_deadline = 0, last_heartbeat = 0, last_phone = 0, last_discovery = 0;
static uint32_t test_id = 0;
static std::deque<uint64_t> pending_events;
static bool known(const uint8_t *bda) {
    int count = esp_bt_gap_get_bond_device_num();
    if (count <= 0 || count > 16)
        return false;
    esp_bd_addr_t a[16];
    if (esp_bt_gap_get_bond_device_list(&count, a) != ESP_OK)
        return false;
    for (int i = 0; i < count; i++)
        if (!memcmp(a[i], bda, 6))
            return true;
    return false;
}
static void record() {
    if (!sdp_ready || !channel_number || record_created)
        return;
    esp_bluetooth_sdp_record_t r = {};
    r.mas.hdr.type = ESP_SDP_TYPE_MAP_MAS;
    r.mas.hdr.service_name = const_cast<char *>("DashBridge Inbox");
    // ESP-IDF's SDP API requires the terminating NUL in this length.
    r.mas.hdr.service_name_length = strlen(r.mas.hdr.service_name) + 1;
    r.mas.hdr.rfcomm_channel_number = channel_number;
    r.mas.hdr.l2cap_psm = -1;
    r.mas.hdr.profile_version = 0x0100;
    r.mas.mas_instance_id = 0;
    r.mas.supported_message_types = 0x02;
    r.mas.supported_features = 0;
    ESP_ERROR_CHECK(esp_sdp_create_record(&r));
    record_created = true;
}
static void drop_mns() {
    if (events.handle)
        esp_spp_disconnect(events.handle);
    events.clear();
    mns_state = 0;
    discovering = false;
    pending_events.clear();
}
static void start_mns() {
    if (!server.handle || !mas.notifications() || mns_state || now() - last_discovery < 5000)
        return;
    last_discovery = now();
    mns_state = 1;
    discovering = true;
    mns_deadline = now() + 10000;
    esp_bt_uuid_t uuid = {};
    uuid.len = ESP_UUID_LEN_16;
    uuid.uuid.uuid16 = ESP_SDP_UUID_MAP_MNS;
    if (esp_sdp_search_record(peer, uuid) != ESP_OK) {
        mns_state = 0;
        discovering = false;
    }
}
static void pump_event() {
    while (!pending_events.empty() && !inbox.find(pending_events.front()))
        pending_events.pop_front();
    if (mns_state == 3 && !pending_events.empty() && mas.notifications()) {
        events.send(mns_event(mns_id, pending_events.front()));
        mns_state = 4;
        mns_deadline = now() + 10000;
    }
}
static void sdp_cb(esp_sdp_cb_event_t event, esp_sdp_cb_param_t *p) {
    Guard g;
    if (event == ESP_SDP_INIT_EVT) {
        sdp_ready = p->init.status == ESP_SDP_SUCCESS;
        record();
    }
    if (event == ESP_SDP_CREATE_RECORD_COMP_EVT) {
        ESP_LOGI(tag, "MAP service record status=%d", p->create_record.status);
    }
    if (event == ESP_SDP_SEARCH_COMP_EVT && discovering) {
        discovering = false;
        if (!server.handle || !mas.notifications() || memcmp(p->search.remote_addr, peer, 6)) {
            mns_state = 0;
            return;
        }
        for (int i = 0; p->search.status == ESP_SDP_SUCCESS && i < p->search.record_count; i++) {
            auto &r = p->search.records[i];
            int scn = r.hdr.rfcomm_channel_number;
            if (r.hdr.type == ESP_SDP_TYPE_MAP_MNS && scn > 0 && scn <= 30) {
                if (esp_spp_connect(ESP_SPP_SEC_AUTHENTICATE | ESP_SPP_SEC_ENCRYPT, ESP_SPP_ROLE_MASTER, scn,
                                    peer) == ESP_OK)
                    return;
            }
        }
        mns_state = 0;
        ESP_LOGW(tag, "Tesla notification service not found yet; will retry");
    }
}
static void spp_cb(esp_spp_cb_event_t e, esp_spp_cb_param_t *p) {
    Guard g;
    switch (e) {
    case ESP_SPP_INIT_EVT:
        if (p->init.status == ESP_SPP_SUCCESS)
            ESP_ERROR_CHECK(esp_spp_start_srv(ESP_SPP_SEC_AUTHENTICATE | ESP_SPP_SEC_ENCRYPT,
                                              ESP_SPP_ROLE_SLAVE, 4, "DashBridge transport"));
        break;
    case ESP_SPP_START_EVT:
        if (p->start.status == ESP_SPP_SUCCESS) {
            channel_number = p->start.scn;
            record();
        }
        break;
    case ESP_SPP_SRV_OPEN_EVT:
        if (server.handle) {
            esp_spp_disconnect(p->srv_open.handle);
            break;
        }
        server.clear();
        server.handle = p->srv_open.handle;
        memcpy(peer, p->srv_open.rem_bda, 6);
        mas.reset();
        inbox.clear();
        pending_events.clear();
        ESP_LOGI(tag, "Tesla message transport connected");
        break;
    case ESP_SPP_OPEN_EVT:
        if (p->open.status != ESP_SPP_SUCCESS) {
            mns_state = 0;
            break;
        }
        if (!server.handle || memcmp(peer, p->open.rem_bda, 6) || !mas.notifications()) {
            esp_spp_disconnect(p->open.handle);
            break;
        }
        events.clear();
        events.handle = p->open.handle;
        events.send(mns_connect());
        mns_state = 2;
        mns_deadline = now() + 10000;
        break;
    case ESP_SPP_CLOSE_EVT:
        if (p->close.handle == server.handle) {
            server.clear();
            mas.reset();
            inbox.clear();
            drop_mns();
            ESP_LOGI(tag, "Tesla disconnected; inbox cleared");
        } else if (p->close.handle == events.handle) {
            events.clear();
            mns_state = 0;
            pending_events.clear();
        }
        break;
    case ESP_SPP_DATA_IND_EVT: {
        auto &d = p->data_ind;
        if (d.handle == server.handle) {
            bool valid = server.framer.feed(d.data, d.len, [](const Bytes &b) {
                auto response = mas.request(b);
                ESP_LOGI(tag, "MAP request 0x%02x -> 0x%02x", b[0], response[0]);
                server.send(std::move(response));
                if (mas.notifications())
                    start_mns();
                else if (mns_state)
                    drop_mns();
            });
            if (!valid)
                esp_spp_disconnect(server.handle);
        } else if (d.handle == events.handle) {
            bool valid = events.framer.feed(d.data, d.len, [](const Bytes &b) {
                if (mns_state == 2) {
                    if (!obex_connection_id(b, mns_id)) {
                        ESP_LOGW(tag, "Notification OBEX connect failed");
                        drop_mns();
                        return;
                    }
                    mns_state = 3;
                    ESP_LOGI(tag, "Ready for new-message notifications");
                } else if (mns_state == 4) {
                    ESP_LOGI(tag, "Tesla event response 0x%02x", b[0]);
                    if (!pending_events.empty())
                        pending_events.pop_front();
                    mns_state = 3;
                }
                pump_event();
            });
            if (!valid)
                drop_mns();
        }
        break;
    }
    case ESP_SPP_WRITE_EVT:
        if (p->write.status != ESP_SPP_SUCCESS) {
            esp_spp_disconnect(p->write.handle);
            break;
        }
        if (p->write.handle == server.handle)
            server.written(p->write.cong);
        else if (p->write.handle == events.handle)
            events.written(p->write.cong);
        break;
    case ESP_SPP_CONG_EVT:
        if (p->cong.handle == server.handle) {
            server.congested = p->cong.cong;
            server.pump();
        } else if (p->cong.handle == events.handle) {
            events.congested = p->cong.cong;
            events.pump();
        }
        break;
    default:
        break;
    }
}
static void gap_cb(esp_bt_gap_cb_event_t e, esp_bt_gap_cb_param_t *p) {
    Guard g;
    if (e == ESP_BT_GAP_CFM_REQ_EVT)
        esp_bt_gap_ssp_confirm_reply(p->cfm_req.bda, pairing_allowed() || known(p->cfm_req.bda));
    if (e == ESP_BT_GAP_PIN_REQ_EVT) {
        esp_bt_pin_code_t pin = {};
        esp_bt_gap_pin_reply(p->pin_req.bda, false, 0, pin);
    }
    if (e == ESP_BT_GAP_AUTH_CMPL_EVT) {
        ESP_LOGI(tag, "Pairing status=%d", p->auth_cmpl.stat);
        if (p->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS)
            paired();
    }
}
static void hfp_cb(esp_hf_cb_event_t e, esp_hf_cb_param_t *p) {
    Guard g;
    // Minimal truthful gateway for car discovery. This prototype has no phone service.
    if (e == ESP_HF_CIND_RESPONSE_EVT)
        esp_hf_ag_cind_response(p->cind_rep.remote_addr, esp_hf_call_status_t(0),
                                esp_hf_call_setup_status_t(0), esp_hf_network_state_t(0), 0,
                                esp_hf_roaming_status_t(0), 5, esp_hf_call_held_status_t(0));
    else if (e == ESP_HF_COPS_RESPONSE_EVT)
        esp_hf_ag_cops_response(p->cops_rep.remote_addr, const_cast<char *>("Notification prototype"));
    else if (e == ESP_HF_CLCC_RESPONSE_EVT)
        esp_hf_ag_clcc_response(p->clcc_rep.remote_addr, 0, esp_hf_current_call_direction_t(0),
                                esp_hf_current_call_status_t(0), esp_hf_current_call_mode_t(0),
                                esp_hf_current_call_mpty_type_t(0), nullptr, ESP_HF_CALL_ADDR_TYPE_UNKNOWN);
    else if (e == ESP_HF_UNAT_RESPONSE_EVT)
        esp_hf_ag_unknown_at_send(p->unat_rep.remote_addr, nullptr);
    else if (e == ESP_HF_DIAL_EVT)
        esp_hf_ag_cmee_send(p->out_call.remote_addr, ESP_HF_AT_RESPONSE_CODE_CME,
                            ESP_HF_CME_OPERATION_NOT_SUPPORTED);
    else if (e == ESP_HF_CNUM_RESPONSE_EVT)
        esp_hf_ag_cnum_response(p->cnum_rep.remote_addr, nullptr, 129,
                                ESP_HF_SUBSCRIBER_SERVICE_TYPE_UNKNOWN);
    else if (e == ESP_HF_CONNECTION_STATE_EVT)
        ESP_LOGI(tag, "Phone-profile state=%d (calls unavailable in prototype)", p->conn_stat.state);
}
void car_receive(const WireMessage &m) {
    last_phone = now();
    if (m.op == Op::reset) {
        pending_events.clear();
        inbox.apply(m);
        return;
    }
    if (m.op == Op::heartbeat)
        return;
    if (!server.handle || !mas.notifications())
        return;
    uint64_t h = inbox.apply(m);
    if (h) {
        ESP_LOGI(tag, "New WhatsApp notification (text is not logged)");
        if (pending_events.size() < 16) {
            pending_events.push_back(h);
            pump_event();
        } else
            ESP_LOGW(tag, "Event queue full");
    }
}
void car_test() {
    if (!server.handle || !mas.notifications() || mns_state < 3) {
        ESP_LOGW(tag, "Not ready: wait for Ready for new-message notifications, then send test again");
        return;
    }
    Notice n;
    n.id = ++test_id;
    n.app = "net.whatsapp.WhatsApp";
    n.title = "DashBridge test";
    n.body = "Your Tesla received a test notification from DashBridge.";
    n.date = "20260920T120000";
    auto h = inbox.apply({Op::add, 0x54455354, n});
    if (h && pending_events.size() < 16) {
        pending_events.push_back(h);
        pump_event();
        ESP_LOGI(tag, "Test notification queued; check the Tesla screen");
    } else
        ESP_LOGW(tag, "Test notification could not be queued; try again later");
}
void car_poll() {
    if (mns_state != 0 && mns_state != 3 && now() > mns_deadline) {
        ESP_LOGW(tag, "Notification transport timed out");
        drop_mns();
    }
    start_mns();
    pump_event();
    if (now() - last_heartbeat >= 1000) {
        last_heartbeat = now();
        Notice n;
        n.id = server.handle && mas.notifications() && mns_state >= 3;
        send({Op::heartbeat, 0, n});
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE,
                                 pairing_allowed() ? ESP_BT_GENERAL_DISCOVERABLE : ESP_BT_NON_DISCOVERABLE);
    }
    if (last_phone && now() - last_phone > 5000) {
        last_phone = 0;
        inbox.clear();
        pending_events.clear();
        ESP_LOGW(tag, "Board A heartbeat lost; inbox cleared");
    }
}
void car_start() {
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(gap_cb));
    ESP_ERROR_CHECK(esp_bt_gap_set_device_name("DashBridge B"));
    esp_bt_io_cap_t cap = ESP_BT_IO_CAP_NONE;
    ESP_ERROR_CHECK(esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &cap, sizeof cap));
    ESP_ERROR_CHECK(esp_hf_ag_register_callback(hfp_cb));
    ESP_ERROR_CHECK(esp_hf_ag_init());
    ESP_ERROR_CHECK(esp_sdp_register_callback(sdp_cb));
    ESP_ERROR_CHECK(esp_sdp_init());
    ESP_ERROR_CHECK(esp_spp_register_callback(spp_cb));
    esp_spp_cfg_t cfg = {};
    cfg.mode = ESP_SPP_MODE_CB;
    cfg.tx_buffer_size = 0;
    ESP_ERROR_CHECK(esp_spp_enhanced_init(&cfg));
    esp_bt_cod_t cod = {};
    cod.major = ESP_BT_COD_MAJOR_DEV_PHONE;
    cod.minor = 3;
    cod.service = ESP_BT_COD_SRVC_TELEPHONY | ESP_BT_COD_SRVC_OBJ_TRANSFER;
    ESP_ERROR_CHECK(esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_ALL));
    ESP_ERROR_CHECK(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE));
}
} // namespace runtime
#endif
