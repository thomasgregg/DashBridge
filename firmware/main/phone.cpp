#include "runtime.hpp"
#include "sdkconfig.h"
#if CONFIG_BRIDGE_PHONE || CONFIG_BRIDGE_SINGLE
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_log.h"
#include "esp_random.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <deque>

namespace runtime {
using namespace bridge;
static const char *tag = "phone";
// UUID bytes in Espressif's little-endian representation.
static uint8_t service_uuid[] = {0xd0, 0x00, 0x2d, 0x12, 0x1e, 0x4b, 0x0f, 0xa4,
                                 0x99, 0x4e, 0xce, 0xb5, 0x31, 0xf4, 0x05, 0x79};
static uint8_t source_uuid[] = {0xbd, 0x1d, 0xa2, 0x99, 0xe6, 0x25, 0x58, 0x8c,
                                0xd9, 0x42, 0x01, 0x63, 0x0d, 0x12, 0xbf, 0x9f};
static uint8_t control_uuid[] = {0xd9, 0xd9, 0xaa, 0xfd, 0xbd, 0x9b, 0x21, 0x98,
                                 0xa8, 0x49, 0xe1, 0x45, 0xf3, 0xd8, 0xd1, 0x69};
static uint8_t data_uuid[] = {0xfb, 0x7b, 0x7c, 0xce, 0x6a, 0xb3, 0x44, 0xbe,
                              0xb5, 0x4b, 0xd6, 0x24, 0xe9, 0xc6, 0xea, 0x22};
// ANCS service solicitation; this accessory does not claim to be a keyboard.
static uint8_t advertisement[] = {2,    0x01, 0x06, 17,   0x15, 0xd0, 0x00, 0x2d, 0x12, 0x1e, 0x4b,
                                  0x0f, 0xa4, 0x99, 0x4e, 0xce, 0xb5, 0x31, 0xf4, 0x05, 0x79};
static esp_ble_adv_params_t advertising = {};
static esp_gatt_if_t interface_id = ESP_GATT_IF_NONE;
static uint16_t connection = 0, start_handle = 0, end_handle = 0;
static uint16_t source_handle = 0, data_handle = 0, control_handle = 0, subscribing = 0;
static esp_bd_addr_t peer = {};
static bool linked = false, secured = false, mtu_ready = false, searching = false, ready = false;
static bool car_ready = false, active = false, canceled = false;
static int adv_pending = 0;
static int64_t last_car = 0, last_heartbeat = 0, deadline = 0, setup_deadline = 0;
static uint32_t session = 0;
struct Request {
    uint32_t id;
    Op op;
};
static std::deque<Request> requests;
static Request current = {};
static AncsResponse response;
static std::vector<std::array<uint8_t, 6>> previous_bonds;
static bool known(const uint8_t *address) {
    for (const auto &bond : previous_bonds)
        if (!memcmp(bond.data(), address, 6))
            return true;
    return false;
}
static void snapshot_bonds() {
    previous_bonds.clear();
    int count = esp_ble_get_bond_device_num();
    if (count <= 0 || count > 16)
        return;
    std::vector<esp_ble_bond_dev_t> bonds(count);
    if (esp_ble_get_bond_device_list(&count, bonds.data()) != ESP_OK)
        return;
    for (int i = 0; i < count; ++i) {
        std::array<uint8_t, 6> address;
        memcpy(address.data(), bonds[i].bd_addr, 6);
        previous_bonds.push_back(address);
    }
}

static void new_session() {
    session = esp_random();
    requests.clear();
    // An outstanding ANCS response must still be drained before another command.
    if (active)
        canceled = true;
    send_to_car({Op::reset, session, {}});
}
static void disconnect(const char *reason) {
    ESP_LOGW(tag, "%s", reason);
    ready = false;
    requests.clear();
    canceled = true;
    if (linked)
        esp_ble_gap_disconnect(peer);
}
static void search() {
    if (!linked || !secured || !mtu_ready || searching)
        return;
    searching = true;
    esp_bt_uuid_t uuid = {};
    uuid.len = ESP_UUID_LEN_128;
    memcpy(uuid.uuid.uuid128, service_uuid, 16);
    if (esp_ble_gattc_search_service(interface_id, connection, &uuid) != ESP_OK)
        disconnect("Could not start ANCS discovery");
}
static void request_next() {
    if (!ready || !car_ready || active || requests.empty())
        return;
    current = requests.front();
    requests.pop_front();
    active = true;
    canceled = false;
    response.clear();
    deadline = now() + 5000;
    Bytes command = {0};
    for (int i = 0; i < 4; ++i)
        command.push_back(current.id >> (i * 8));
    // App identifier, title (128), subtitle (128), body (768), date.
    const uint8_t attrs[] = {0, 1, 128, 0, 2, 128, 0, 3, 0, 3, 5};
    command.insert(command.end(), std::begin(attrs), std::end(attrs));
    if (esp_ble_gattc_write_char(interface_id, connection, control_handle, command.size(), command.data(),
                                 ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE) != ESP_OK)
        disconnect("Could not request notification details");
}
static void source_event(const uint8_t *value, size_t size) {
    if (size != 8 || value[0] > 2)
        return;
    uint32_t id =
        uint32_t(value[4]) | uint32_t(value[5]) << 8 | uint32_t(value[6]) << 16 | uint32_t(value[7]) << 24;
    ESP_LOGI(tag, "ANCS event=%u flags=0x%02x uid=%lu car_ready=%d preexisting=%d",
             unsigned(value[0]), unsigned(value[1]), static_cast<unsigned long>(id),
             car_ready, ancs_is_preexisting(value[1]));
    if (value[0] == 2) {
        requests.erase(
            std::remove_if(requests.begin(), requests.end(), [id](const Request &r) { return r.id == id; }),
            requests.end());
        if (active && current.id == id)
            canceled = true;
        if (car_ready) {
            Notice n;
            n.id = id;
            send_to_car({Op::remove, session, n});
        }
        return;
    }
    if (!car_ready || ancs_is_preexisting(value[1]))
        return; // Ignore pre-existing notifications.
    Op op = value[0] == 0 ? Op::add : Op::update;
    for (auto &r : requests)
        if (r.id == id)
            return; // Coalesce pending updates.
    if (requests.size() >= 32) {
        ESP_LOGW(tag, "Notification queue full; dropping newest");
        return;
    }
    // If an add is already in flight, its subsequent change is an update.
    if (active && current.id == id)
        op = Op::update;
    requests.push_back({id, op});
    request_next();
}
static void subscribe(uint16_t handle) {
    subscribing = handle;
    if (esp_ble_gattc_register_for_notify(interface_id, peer, handle) != ESP_OK)
        disconnect("Could not register ANCS subscription");
}
static void gap(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *p) {
    Guard guard;
    switch (event) {
    case ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT: {
        if (p->local_privacy_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(tag, "Bluetooth privacy setup failed");
            break;
        }
        adv_pending = 2;
        ESP_ERROR_CHECK(esp_ble_gap_config_adv_data_raw(advertisement, sizeof advertisement));
        esp_ble_adv_data_t scan = {};
        scan.set_scan_rsp = true;
        scan.include_name = true;
        ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&scan));
        break;
    }
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        if (--adv_pending == 0)
            ESP_ERROR_CHECK(esp_ble_gap_start_advertising(&advertising));
        break;
    case ESP_GAP_BLE_SEC_REQ_EVT:
        esp_ble_gap_security_rsp(p->ble_security.ble_req.bd_addr,
                                 pairing_allowed(Peer::phone) || known(p->ble_security.ble_req.bd_addr));
        break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        if (!p->ble_security.auth_cmpl.success) {
            disconnect("iPhone pairing/encryption failed");
            break;
        }
        if (!pairing_allowed(Peer::phone) && !known(p->ble_security.auth_cmpl.bd_addr)) {
            esp_ble_remove_bond_device(p->ble_security.auth_cmpl.bd_addr);
            disconnect("Unrecognized phone; enter pair phone in USB console");
            break;
        }
        secured = true;
        paired(Peer::phone);
        ESP_LOGI(tag, "iPhone link encrypted");
        search();
        break;
    default:
        break;
    }
}
static void gatt(esp_gattc_cb_event_t event, esp_gatt_if_t id, esp_ble_gattc_cb_param_t *p) {
    Guard guard;
    switch (event) {
    case ESP_GATTC_REG_EVT:
        if (p->reg.status != ESP_GATT_OK) {
            ESP_LOGE(tag, "GATT registration failed");
            break;
        }
        interface_id = id;
        #if CONFIG_BRIDGE_SINGLE
        ESP_ERROR_CHECK(esp_ble_gap_set_device_name("DashBridge"));
#else
        ESP_ERROR_CHECK(esp_ble_gap_set_device_name("DashBridge A"));
#endif
        ESP_ERROR_CHECK(esp_ble_gap_config_local_privacy(true));
        break;
    case ESP_GATTC_CONNECT_EVT: {
        if (linked)
            break;
        linked = true;
        connection = p->connect.conn_id;
        snapshot_bonds();
        memcpy(peer, p->connect.remote_bda, 6);
        setup_deadline = now() + 60000;
        new_session();
        esp_ble_gatt_creat_conn_params_t params = {};
        memcpy(params.remote_bda, peer, 6);
        params.remote_addr_type = p->connect.ble_addr_type;
        params.own_addr_type = BLE_ADDR_TYPE_RPA_PUBLIC;
        params.is_direct = true;
        if (esp_ble_gattc_enh_open(interface_id, &params) != ESP_OK)
            disconnect("Could not open iPhone GATT connection");
        break;
    }
    case ESP_GATTC_OPEN_EVT:
        if (p->open.status != ESP_GATT_OK) {
            disconnect("GATT open failed");
            break;
        }
        connection = p->open.conn_id;
        if (esp_ble_set_encryption(peer, ESP_BLE_SEC_ENCRYPT) != ESP_OK) {
            disconnect("Could not encrypt iPhone link");
            break;
        }
        if (esp_ble_gattc_send_mtu_req(interface_id, connection) != ESP_OK) {
            mtu_ready = true;
            search();
        }
        break;
    case ESP_GATTC_CFG_MTU_EVT:
        mtu_ready = true;
        search();
        break;
    case ESP_GATTC_SEARCH_RES_EVT:
        if (p->search_res.srvc_id.uuid.len == ESP_UUID_LEN_128 &&
            !memcmp(p->search_res.srvc_id.uuid.uuid.uuid128, service_uuid, 16)) {
            start_handle = p->search_res.start_handle;
            end_handle = p->search_res.end_handle;
        }
        break;
    case ESP_GATTC_SEARCH_CMPL_EVT: {
        if (p->search_cmpl.status != ESP_GATT_OK || !start_handle) {
            disconnect("ANCS unavailable; allow Share System Notifications on iPhone");
            break;
        }
        esp_gattc_char_elem_t chars[16];
        uint16_t count = 16;
        if (esp_ble_gattc_get_all_char(interface_id, connection, start_handle, end_handle, chars, &count,
                                       0) != ESP_GATT_OK) {
            disconnect("ANCS characteristic discovery failed");
            break;
        }
        for (unsigned i = 0; i < count; ++i)
            if (chars[i].uuid.len == ESP_UUID_LEN_128) {
                auto u = chars[i].uuid.uuid.uuid128;
                if (!memcmp(u, source_uuid, 16))
                    source_handle = chars[i].char_handle;
                if (!memcmp(u, data_uuid, 16))
                    data_handle = chars[i].char_handle;
                if (!memcmp(u, control_uuid, 16))
                    control_handle = chars[i].char_handle;
            }
        if (!source_handle || !data_handle || !control_handle) {
            disconnect("Incomplete ANCS service");
            break;
        }
        subscribe(data_handle); // Details channel must be ready before new events arrive.
        break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
        if (p->reg_for_notify.status != ESP_GATT_OK) {
            disconnect("ANCS notification registration failed");
            break;
        }
        esp_bt_uuid_t uuid = {};
        uuid.len = ESP_UUID_LEN_16;
        uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
        esp_gattc_descr_elem_t descriptor;
        uint16_t count = 1;
        if (esp_ble_gattc_get_descr_by_char_handle(interface_id, connection, p->reg_for_notify.handle, uuid,
                                                   &descriptor, &count) != ESP_GATT_OK ||
            count != 1) {
            disconnect("ANCS subscription descriptor missing");
            break;
        }
        uint8_t enable[] = {1, 0};
        if (esp_ble_gattc_write_char_descr(interface_id, connection, descriptor.handle, 2, enable,
                                           ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE) != ESP_OK)
            disconnect("ANCS subscription write failed");
        break;
    }
    case ESP_GATTC_WRITE_DESCR_EVT:
        if (p->write.status != ESP_GATT_OK) {
            disconnect("iPhone refused notification access");
            break;
        }
        if (subscribing == data_handle)
            subscribe(source_handle);
        else {
            ready = true;
            ESP_LOGI(tag, "ANCS ready; waiting for car and new notifications");
            request_next();
        }
        break;
    case ESP_GATTC_NOTIFY_EVT:
        if (p->notify.handle == source_handle)
            source_event(p->notify.value, p->notify.value_len);
        else if (p->notify.handle == data_handle && active) {
            Notice notice;
            int result = response.feed(p->notify.value, p->notify.value_len, current.id, notice);
            if (result < 0) {
                disconnect("Invalid notification response; reconnecting");
                break;
            }
            if (result == 1) {
                bool whatsapp = notice.app == "net.whatsapp.WhatsApp" || notice.app == "net.whatsapp.WhatsAppSMB";
                ESP_LOGI(tag, "ANCS details uid=%lu whatsapp=%d canceled=%d car_ready=%d; %s",
                         static_cast<unsigned long>(current.id), whatsapp, canceled, car_ready,
                         !canceled && car_ready && whatsapp ? "forwarding" : "filtered");
                if (!canceled && car_ready && whatsapp)
                    send_to_car({current.op, session, notice});
                active = false;
                request_next();
            }
        }
        break;
    case ESP_GATTC_WRITE_CHAR_EVT:
        if (p->write.status != ESP_GATT_OK) {
            // The notification may already have disappeared. Reset the stream safely.
            disconnect("Notification request rejected; reconnecting");
        }
        break;
    case ESP_GATTC_SRVC_CHG_EVT:
        esp_ble_gattc_cache_refresh(peer);
        disconnect("iPhone services changed; rediscovering");
        break;
    case ESP_GATTC_DISCONNECT_EVT:
        linked = secured = mtu_ready = searching = ready = active = false;
        start_handle = end_handle = source_handle = data_handle = control_handle = subscribing = 0;
        response.clear();
        new_session();
        ESP_LOGI(tag, "iPhone disconnected; inbox cleared");
        esp_ble_gap_start_advertising(&advertising);
        break;
    default:
        break;
    }
}
void phone_receive(const WireMessage &m) {
    if (m.op != Op::heartbeat)
        return;
    last_car = now();
    bool available = m.notice.id == 1;
    if (available != car_ready) {
        car_ready = available;
        new_session();
    }
}
void phone_poll() {
    if (now() - last_heartbeat >= 1000) {
        last_heartbeat = now();
        send_to_car({Op::heartbeat, session, {}});
    }
    if (car_ready && now() - last_car > 3000) {
        car_ready = false;
        new_session();
    }
    if (linked && !ready && now() > setup_deadline) {
        setup_deadline = now() + 60000;
        disconnect("iPhone setup timed out");
    }
    if (active && now() > deadline) {
        active = false;
        response.clear();
        disconnect("Notification response timed out");
    }
    request_next();
}
bool phone_notifications_ready() { return linked && secured && ready; }
void phone_start() {
    advertising.adv_int_min = 0x100;
    advertising.adv_int_max = 0x100;
    advertising.adv_type = ADV_TYPE_IND;
    advertising.own_addr_type = BLE_ADDR_TYPE_RPA_PUBLIC;
    advertising.channel_map = ADV_CHNL_ALL;
    advertising.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
    esp_ble_auth_req_t auth = ESP_LE_AUTH_REQ_SC_BOND;
    esp_ble_io_cap_t cap = ESP_IO_CAP_NONE;
    uint8_t key_size = 16, keys = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth, sizeof auth));
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &cap, sizeof cap));
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof key_size));
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &keys, sizeof keys));
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &keys, sizeof keys));
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap));
    ESP_ERROR_CHECK(esp_ble_gattc_register_callback(gatt));
    ESP_ERROR_CHECK(esp_ble_gattc_app_register(0));
    new_session();
}
} // namespace runtime
#endif
