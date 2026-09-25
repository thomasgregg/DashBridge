#include "dashbridge/adapters/features.hpp"
#include "dashbridge/ports/runtime_services.hpp"
#include "dashbridge/adapters/map_adapter.hpp"
#include "dashbridge/adapters/pbap_adapter.hpp"
#include "dashbridge/protocols/obex.hpp"
#include "sdkconfig.h"
#if CONFIG_BRIDGE_CAR
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_hf_ag_api.h"
#include "esp_log.h"
#include "esp_sdp_api.h"
#include "esp_spp_api.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
namespace runtime {
using namespace dashbridge::ports;
namespace messages = dashbridge::core::messages;
namespace dashlink = dashbridge::protocols::dashlink_v2;
namespace map_adapter = dashbridge::adapters::car::map;
namespace pbap_adapter = dashbridge::adapters::car::pbap;
namespace obex = dashbridge::protocols::obex;
enum class ObexService { unknown, map, pbap };
static const char *tag = "car";
static messages::Store inbox;
static map_adapter::Server mas(inbox);
#if CONFIG_BRIDGE_CONTACT_SYNC
static pbap_adapter::Server pbap(contacts_phonebook());
#endif
struct Channel {
    uint32_t handle = 0;
    bool writing = false, congested = false;
    std::deque<obex::Bytes> queue;
    obex::Framer framer;
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
    void send(obex::Bytes b) {
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
struct Inbound {
    Channel io;
    ObexService service = ObexService::unknown;
    void clear() { io.clear(); service = ObexService::unknown; }
};
static std::array<Inbound, 2> inbound;
static Channel events;
static Channel *map_transport() {
    for (auto &candidate : inbound) if (candidate.service == ObexService::map) return &candidate.io;
    return nullptr;
}
static Inbound *inbound_transport(uint32_t handle) {
    for (auto &candidate : inbound) if (candidate.io.handle == handle) return &candidate;
    return nullptr;
}
static bool service_open(ObexService service, const Inbound *except = nullptr) {
    for (auto &candidate : inbound) if (&candidate != except && candidate.service == service) return true;
    return false;
}
static esp_bd_addr_t peer = {};
static bool sdp_ready = false, map_record_created = false, discovering = false;
static uint8_t map_channel_number = 0;
#if CONFIG_BRIDGE_CONTACT_SYNC
static bool pbap_record_created = false;
static uint8_t pbap_channel_number = 0;
#endif
static int mns_state = 0; // 0 idle, 1 discovery/connect, 2 OBEX connect, 3 ready, 4 event awaiting response
static uint32_t mns_id = 0;
static int64_t mns_deadline = 0, last_heartbeat = 0, last_phone = 0, last_discovery = 0;
static int discoverable = -1;
static uint32_t phone_flags = 0;
static uint32_t test_id = 0;
static std::deque<uint64_t> pending_events;
static void record() {
    if (!sdp_ready) return;
    if (map_channel_number && !map_record_created) {
        esp_bluetooth_sdp_record_t r = {};
        r.mas.hdr.type = ESP_SDP_TYPE_MAP_MAS;
        r.mas.hdr.service_name = const_cast<char *>("DashBridge Inbox");
        r.mas.hdr.service_name_length = strlen(r.mas.hdr.service_name) + 1;
        r.mas.hdr.rfcomm_channel_number = map_channel_number;
        r.mas.hdr.l2cap_psm = -1;
        r.mas.hdr.profile_version = 0x0100;
        r.mas.mas_instance_id = 0;
        r.mas.supported_message_types = 0x02;
        r.mas.supported_features = 0;
        ESP_ERROR_CHECK(esp_sdp_create_record(&r));
        map_record_created = true;
    }
#if CONFIG_BRIDGE_CONTACT_SYNC
    if (pbap_channel_number && !pbap_record_created) {
        esp_bluetooth_sdp_record_t r = {};
        r.pse.hdr.type = ESP_SDP_TYPE_PBAP_PSE;
        r.pse.hdr.service_name = const_cast<char *>("DashBridge Contacts");
        r.pse.hdr.service_name_length = strlen(r.pse.hdr.service_name) + 1;
        r.pse.hdr.rfcomm_channel_number = pbap_channel_number;
        r.pse.hdr.l2cap_psm = -1;
        r.pse.hdr.profile_version = 0x0102;
        // Local phonebook/call history plus the PBAP 1.2 Favorites repository.
        r.pse.supported_repositories = 0x09;
        r.pse.supported_features = 0x0003;
        ESP_ERROR_CHECK(esp_sdp_create_record(&r));
        pbap_record_created = true;
    }
#endif
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
    auto map = map_transport();
    if (!map || !map->handle || !mas.notifications() || mns_state || now() - last_discovery < 5000)
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
        events.send(map_adapter::notification_event(mns_id, pending_events.front()));
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
        ESP_LOGI(tag, "Bluetooth object service record status=%d", p->create_record.status);
    }
    if (event == ESP_SDP_SEARCH_COMP_EVT && discovering) {
        discovering = false;
        auto map = map_transport();
        if (!map || !map->handle || !mas.notifications() || memcmp(p->search.remote_addr, peer, 6)) {
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
static void start_object_server(uint8_t channel, const char *name) {
    esp_spp_start_srv_cfg_t config = {};
    config.local_scn = channel;
    config.create_spp_record = false;
    config.sec_mask = ESP_SPP_SEC_AUTHENTICATE | ESP_SPP_SEC_ENCRYPT;
    config.role = ESP_SPP_ROLE_SLAVE;
    config.name = name;
    ESP_ERROR_CHECK(esp_spp_start_srv_with_cfg(&config));
}
static void spp_cb(esp_spp_cb_event_t e, esp_spp_cb_param_t *p) {
    Guard g;
    switch (e) {
    case ESP_SPP_INIT_EVT:
        if (p->init.status == ESP_SPP_SUCCESS) {
            start_object_server(4, "DashBridge messages");
#if CONFIG_BRIDGE_CONTACT_SYNC
            start_object_server(5, "DashBridge contacts");
#endif
        }
        break;
    case ESP_SPP_START_EVT:
        if (p->start.status == ESP_SPP_SUCCESS) {
            if (p->start.scn == 4) map_channel_number = p->start.scn;
#if CONFIG_BRIDGE_CONTACT_SYNC
            else if (p->start.scn == 5) pbap_channel_number = p->start.scn;
#endif
            record();
        }
        break;
    case ESP_SPP_SRV_OPEN_EVT: {
        Inbound *connection = nullptr;
        for (auto &candidate : inbound) if (!candidate.io.handle) { connection = &candidate; break; }
        if (!connection || (map_transport() && memcmp(peer, p->srv_open.rem_bda, 6))) {
            esp_spp_disconnect(p->srv_open.handle);
            break;
        }
        connection->clear();
        connection->io.handle = p->srv_open.handle;
        memcpy(peer, p->srv_open.rem_bda, 6);
        ESP_LOGI(tag, "Tesla object transport connected; awaiting profile selection");
        break;
    }
    case ESP_SPP_OPEN_EVT:
        if (p->open.status != ESP_SPP_SUCCESS) {
            mns_state = 0;
            break;
        }
        if (!map_transport() || memcmp(peer, p->open.rem_bda, 6) || !mas.notifications()) {
            esp_spp_disconnect(p->open.handle);
            break;
        }
        events.clear();
        events.handle = p->open.handle;
        events.send(map_adapter::notification_connect());
        mns_state = 2;
        mns_deadline = now() + 10000;
        break;
    case ESP_SPP_CLOSE_EVT: {
        auto connection = inbound_transport(p->close.handle);
        if (connection) {
            if (connection->service == ObexService::map) {
                mas.reset(); inbox.clear(); drop_mns();
                ESP_LOGI(tag, "Tesla message service disconnected; inbox cleared");
            }
#if CONFIG_BRIDGE_CONTACT_SYNC
            else if (connection->service == ObexService::pbap) {
                pbap.reset();
                ESP_LOGI(tag, "Tesla contact-name service disconnected");
            }
#endif
            connection->clear();
        } else if (p->close.handle == events.handle) {
            events.clear();
            mns_state = 0;
            pending_events.clear();
        }
        break;
    }
    case ESP_SPP_DATA_IND_EVT: {
        auto &d = p->data_ind;
        auto connection = inbound_transport(d.handle);
        if (connection) {
            bool valid = connection->io.framer.feed(d.data, d.len, [connection](const obex::Bytes &b) {
                if (connection->service == ObexService::unknown) {
                    auto selected = map_adapter::accepts_connect(b) ? ObexService::map : ObexService::unknown;
#if CONFIG_BRIDGE_CONTACT_SYNC
                    if (selected == ObexService::unknown && pbap_adapter::Server::accepts_connect(b))
                        selected = ObexService::pbap;
#endif
#if !CONFIG_BRIDGE_CONTACT_SYNC
                    if (selected == ObexService::pbap) selected = ObexService::unknown;
#endif
                    if (selected == ObexService::unknown || service_open(selected, connection)) {
                        ESP_LOGW(tag, "Rejected unknown or duplicate Tesla object service");
                        esp_spp_disconnect(connection->io.handle);
                        return;
                    }
                    connection->service = selected;
                    if (selected == ObexService::map) {
                        mas.reset(); inbox.clear(); pending_events.clear();
                        ESP_LOGI(tag, "Tesla message service selected");
                    }
#if CONFIG_BRIDGE_CONTACT_SYNC
                    else {
                        pbap.reset();
                        ESP_LOGI(tag, "Tesla contact-name service selected");
                    }
#endif
                }
                obex::Bytes response;
                if (connection->service == ObexService::map) response = mas.request(b);
#if CONFIG_BRIDGE_CONTACT_SYNC
                else response = pbap.request(b);
#endif
                if (response.empty()) { esp_spp_disconnect(connection->io.handle); return; }
                ESP_LOGI(tag, "%s request 0x%02x -> 0x%02x",
                         connection->service == ObexService::map ? "MAP" : "PBAP", b[0], response[0]);
                connection->io.send(std::move(response));
                if (connection->service == ObexService::map) {
                    if (mas.notifications()) start_mns();
                    else if (mns_state) drop_mns();
                }
            });
            if (!valid)
                esp_spp_disconnect(connection->io.handle);
        } else if (d.handle == events.handle) {
            bool valid = events.framer.feed(d.data, d.len, [](const obex::Bytes &b) {
                if (mns_state == 2) {
                    if (!map_adapter::notification_connection_id(b, mns_id)) {
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
        if (auto connection = inbound_transport(p->write.handle))
            connection->io.written(p->write.cong);
        else if (p->write.handle == events.handle)
            events.written(p->write.cong);
        break;
    case ESP_SPP_CONG_EVT:
        if (auto connection = inbound_transport(p->cong.handle)) {
            connection->io.congested = p->cong.cong;
            connection->io.pump();
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
    if (e == ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT) {
        ESP_LOGI(tag, "Bluetooth link opened: status=%d handle=0x%x bonded=%d",
                 p->acl_conn_cmpl_stat.stat, p->acl_conn_cmpl_stat.handle, classic_bond_known(p->acl_conn_cmpl_stat.bda));
    } else if (e == ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT) {
        ESP_LOGW(tag, "Bluetooth link closed: reason=0x%02x handle=0x%x bonded=%d",
                 p->acl_disconn_cmpl_stat.reason, p->acl_disconn_cmpl_stat.handle, classic_bond_known(p->acl_disconn_cmpl_stat.bda));
    }
    if (e == ESP_BT_GAP_CFM_REQ_EVT)
        esp_bt_gap_ssp_confirm_reply(p->cfm_req.bda,
                                     pairing_allowed(Peer::car) || classic_bond_known(p->cfm_req.bda));
    if (e == ESP_BT_GAP_PIN_REQ_EVT) {
        esp_bt_pin_code_t pin = {};
        esp_bt_gap_pin_reply(p->pin_req.bda, false, 0, pin);
    }
    if (e == ESP_BT_GAP_AUTH_CMPL_EVT) {
        ESP_LOGI(tag, "Pairing status=%d", p->auth_cmpl.stat);
        if (p->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS)
            paired(Peer::car);
    }
}
#if !CONFIG_BRIDGE_CALL_RELAY
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
#endif
void car_receive(const dashlink::Heartbeat &heartbeat) {
    if (heartbeat.source != dashlink::Board::phone || !heartbeat.session) return;
    last_phone = now();
    phone_flags = heartbeat.flags;
}
void car_receive(const dashlink::Notification &notification) {
    messages::Change change{notification.kind, notification.session, notification.message};
    last_phone = now();
    if (notification.kind == messages::ChangeKind::reset) {
        pending_events.clear();
        inbox.apply(change);
        return;
    }
    if (!map_transport() || !mas.notifications())
        return;
    uint64_t h = inbox.apply(change);
    if (h) {
        ESP_LOGI(tag, "Allowed app notification (text is not logged)");
        if (pending_events.size() < 16) {
            pending_events.push_back(h);
            pump_event();
        } else
            ESP_LOGW(tag, "Event queue full");
    }
}
bool car_test() {
    if (!car_notifications_ready()) {
        ESP_LOGW(tag, "Not ready: wait for Ready for new-message notifications, then send test again");
        return false;
    }
    messages::Message n;
    n.id = ++test_id;
    n.app = "DashBridge";
    n.title = "DashBridge test";
    n.body = "Your Tesla received a test notification from DashBridge.";
    n.date = "20260920T120000";
    auto h = inbox.apply({messages::ChangeKind::local_test, 0, n});
    if (h && pending_events.size() < 16) {
        pending_events.push_back(h);
        pump_event();
        ESP_LOGI(tag, "Test notification queued; check the Tesla screen");
        return true;
    } else
        ESP_LOGW(tag, "Test notification could not be queued; try again later");
    return false;
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
        uint32_t flags = (car_notifications_ready() ? 1 : 0) | (map_transport() ? 4 : 0) |
                         (mas.notifications() ? 8 : 0);
#if CONFIG_BRIDGE_CALL_RELAY
        if (relay_calls_ready()) flags |= 2;
#endif
        send_to_phone(dashlink::Heartbeat{dashlink::Board::car, 0, flags});
        const int desired = pairing_allowed(Peer::car);
        if (desired != discoverable) {
            auto error = esp_bt_gap_set_scan_mode(
                ESP_BT_CONNECTABLE,
                desired ? ESP_BT_GENERAL_DISCOVERABLE : ESP_BT_NON_DISCOVERABLE);
            if (error == ESP_OK) {
                discoverable = desired;
                ESP_LOGI(tag, "Discoverability changed: %s", desired ? "pairing" : "paired-only");
            } else {
                ESP_LOGW(tag, "Discoverability change failed: %s", esp_err_to_name(error));
            }
        }
    }
    if (last_phone && now() - last_phone > 5000) {
        last_phone = 0;
        phone_flags = 0;
        inbox.clear();
        pending_events.clear();
#if CONFIG_BRIDGE_CONTACT_SYNC
        contacts_phonebook().clear();
#endif
        ESP_LOGW(tag, "Board A heartbeat lost; inbox cleared");
    }
}
bool car_notifications_ready() { return map_transport() && mas.notifications() && mns_state >= 3; }
bool car_message_transport_ready() { return map_transport() != nullptr; }
bool car_message_sync_ready() { return mas.notifications(); }
bool car_board_link_ready() { return last_phone && now() - last_phone < 5000; }
bool car_phone_notification_ready() { return car_board_link_ready() && (phone_flags & 1); }
bool car_phone_bluetooth_ready() { return car_board_link_ready() && (phone_flags & 4); }
bool car_phone_call_ready() { return car_board_link_ready() && (phone_flags & 2); }
void car_start() {
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(gap_cb));
    ESP_ERROR_CHECK(esp_bt_gap_set_device_name("Dash Tesla"));
    esp_bt_io_cap_t cap = ESP_BT_IO_CAP_NONE;
    ESP_ERROR_CHECK(esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &cap, sizeof cap));
#if !CONFIG_BRIDGE_CALL_RELAY
    ESP_ERROR_CHECK(esp_hf_ag_register_callback(hfp_cb));
    ESP_ERROR_CHECK(esp_hf_ag_init());
#endif
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
#if CONFIG_BRIDGE_MUSIC_RELAY
    cod.service |= ESP_BT_COD_SRVC_AUDIO | ESP_BT_COD_SRVC_CAPTURING;
#endif
    ESP_ERROR_CHECK(esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_ALL));
    ESP_ERROR_CHECK(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE));
}
} // namespace runtime
#endif
