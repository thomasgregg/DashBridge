#include "dashbridge/adapters/features.hpp"
#include "dashbridge/adapters/ancs_codec.hpp"
#include "dashbridge/ports/runtime_services.hpp"
#include "sdkconfig.h"
#if CONFIG_BRIDGE_PHONE
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_log.h"
#include "nvs.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <deque>

namespace runtime {
using namespace dashbridge::ports;
namespace ancs = dashbridge::adapters::ancs;
namespace messages = dashbridge::core::messages;
namespace setup = dashbridge::core::setup;
namespace dashlink = dashbridge::protocols::dashlink_v2;
using Bytes = ancs::Bytes;
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
// Keep ANCS solicitation and add the generic HID discovery advertisement used by
// ESP-IDF v5.5.5 examples/bluetooth/bluedroid/ble/ble_ancs. This is a compatibility
// experiment for iOS Settings discovery, not a keyboard/report implementation.
static uint8_t advertisement[] = {2,    0x01, 0x06, 17,   0x15, 0xd0, 0x00, 0x2d, 0x12, 0x1e, 0x4b,
                                  0x0f, 0xa4, 0x99, 0x4e, 0xce, 0xb5, 0x31, 0xf4, 0x05, 0x79,
                                  3, 0x03, 0x12, 0x18, // Complete 16-bit service list: HID, 0x1812.
                                  3, 0x19, 0xc0, 0x03}; // Appearance: generic HID, 0x03c0.
static_assert(sizeof(advertisement) <= 31, "BLE advertisement exceeds 31 bytes");
static esp_ble_adv_params_t advertising = {};
static esp_gatt_if_t interface_id = ESP_GATT_IF_NONE;
static uint16_t connection = 0, start_handle = 0, end_handle = 0;
static uint16_t setup_connection = UINT16_MAX;
static uint16_t source_handle = 0, data_handle = 0, control_handle = 0, subscribing = 0;
static esp_bd_addr_t peer = {};
static bool linked = false, secured = false, mtu_ready = false, searching = false, ready = false;
static bool car_ready = false, active = false, canceled = false;
enum class Fetch { none, identity, app_name, details };
static Fetch fetch = Fetch::none;
struct SeenApp { std::string id, name; bool named = false; };
static std::deque<SeenApp> seen_apps;
static ancs::ApplicationIdResponse identity_response;
static ancs::ApplicationNameResponse name_response;
static std::string current_app;
static int64_t discover_until = 0;
static int64_t test_notification_until = 0;
static const char *test_app_id = "dev.dashbridge.companion";
static const setup::ApplicationRule test_rule = {test_app_id, "DashBridge test", setup::Preview::full};
static bool notification_rule(const std::string &id, setup::ApplicationRule &rule) {
    if (id == test_app_id && now() < test_notification_until) {
        rule = test_rule;
        return true;
    }
    return setup_controller().find_application_rule(id, rule);
}
static uint32_t car_status = 0;
static int adv_pending = 0;
// Diagnostic state only; never used to drive pairing or connection decisions.
static const char *adv_state = "not requested";
static int privacy_status = -1, adv_data_status = -1, scan_data_status = -1;
static int64_t last_car = 0, last_heartbeat = 0, deadline = 0, setup_deadline = 0;
static uint32_t session = 0;
static messages::Store message_state;
struct Request {
    uint32_t id;
    messages::ChangeKind kind;
};
static std::deque<Request> requests;
static Request current = {};
static ancs::NotificationResponse response;
static void request_next();
static void disconnect(const char *reason);
static std::vector<std::array<uint8_t, 6>> previous_bonds;
static void apply_message_change(messages::ChangeKind kind, const messages::Message &notice = {}) {
    message_state.apply({kind, session, notice});
}
static esp_err_t log_request(const char *operation, esp_err_t result) {
    ESP_LOGI(tag, "BLE %s request: %s (0x%x)", operation, esp_err_to_name(result), unsigned(result));
    return result;
}
static esp_err_t start_advertising() {
    adv_state = "starting";
    esp_err_t result = log_request("advertising start", esp_ble_gap_start_advertising(&advertising));
    if (result != ESP_OK)
        adv_state = "request failed";
    return result;
}
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
    session = random_token(); // Zero identifies Board B's heartbeats.
    apply_message_change(messages::ChangeKind::reset);
    requests.clear();
    // An outstanding ANCS response must still be drained before another command.
    if (active)
        canceled = true;
    send_to_car(dashlink::Notification{messages::ChangeKind::reset, session, {}});
}
static void load_policy() {
    nvs_handle_t h;
    if (nvs_open("appcfg", NVS_READONLY, &h) != ESP_OK) return;
    size_t n = 0;
    auto e = nvs_get_blob(h, "rules", nullptr, &n);
    if (e == ESP_OK && n <= 2048) {
        Bytes b(n);
        e = nvs_get_blob(h, "rules", b.data(), &n);
        if (e == ESP_OK && setup_controller().restore_policy(b)) ESP_LOGI(tag, "App choices loaded");
        else ESP_LOGW(tag, "App choices invalid; using empty policy");
    }
    nvs_close(h);
}
bool phone_setup_save_policy(const dashbridge::core::setup::Bytes &bytes) {
    nvs_handle_t h;
    if (nvs_open("appcfg", NVS_READWRITE, &h) != ESP_OK) return false;
    auto e = nvs_set_blob(h, "rules", bytes.data(), bytes.size());
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK;
}
static SeenApp *seen(const std::string &id) {
    for (auto &entry : seen_apps) if (entry.id == id) return &entry;
    return nullptr;
}
std::string phone_setup_recent_application_name(std::string_view id) {
    if (auto *entry = seen(std::string(id))) return entry->name;
    return {};
}
static void remember(const std::string &id, const std::string &name = {}, bool named = false) {
    if (auto *entry = seen(id)) {
        if (named) { entry->name = name; entry->named = true; }
        return;
    }
    if (seen_apps.size() == 12) seen_apps.pop_front();
    seen_apps.push_back({id, named ? name : setup::application_name_fallback(id), named});
}
static void finish_request() {
    active = false;
    fetch = Fetch::none;
    current_app.clear();
    request_next();
}
static void request_details() {
    fetch = Fetch::details;
    response.clear();
    deadline = now() + 5000;
    Bytes command = {0};
    for (int i = 0; i < 4; ++i) command.push_back(current.id >> (i * 8));
    const uint8_t attrs[] = {0, 1, 128, 0, 2, 128, 0, 3, 0, 3, 5};
    command.insert(command.end(), std::begin(attrs), std::end(attrs));
    if (esp_ble_gattc_write_char(interface_id, connection, control_handle, command.size(), command.data(),
                                 ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE) != ESP_OK)
        disconnect("Could not request notification details");
}
static void request_app_name() {
    fetch = Fetch::app_name;
    name_response.clear();
    deadline = now() + 5000;
    Bytes command = {1};
    command.insert(command.end(), current_app.begin(), current_app.end());
    command.push_back(0); command.push_back(0);
    if (esp_ble_gattc_write_char(interface_id, connection, control_handle, command.size(), command.data(),
                                 ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE) != ESP_OK)
        disconnect("Could not request app name");
}
static void disconnect(const char *reason) {
    ESP_LOGW(tag, "%s", reason);
    phone_status();
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
    if (log_request("ANCS discovery", esp_ble_gattc_search_service(interface_id, connection, &uuid)) != ESP_OK)
        disconnect("Could not start ANCS discovery");
}
static void request_next() {
    if (!ready || (!car_ready && now() >= discover_until) || active || requests.empty())
        return;
    current = requests.front();
    requests.pop_front();
    active = true;
    canceled = false;
    fetch = Fetch::identity;
    identity_response.clear();
    deadline = now() + 5000;
    Bytes command = {0};
    for (int i = 0; i < 4; ++i)
        command.push_back(current.id >> (i * 8));
    command.push_back(0); // Ask only for the app identifier before touching notification content.
    if (esp_ble_gattc_write_char(interface_id, connection, control_handle, command.size(), command.data(),
                                 ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE) != ESP_OK)
        disconnect("Could not request app identity");
}
static void source_event(const uint8_t *value, size_t size) {
    if (size != 8 || value[0] > 2)
        return;
    uint32_t id =
        uint32_t(value[4]) | uint32_t(value[5]) << 8 | uint32_t(value[6]) << 16 | uint32_t(value[7]) << 24;
    ESP_LOGI(tag, "ANCS event=%u flags=0x%02x uid=%lu car_ready=%d preexisting=%d",
             unsigned(value[0]), unsigned(value[1]), static_cast<unsigned long>(id),
             car_ready, ancs::is_preexisting(value[1]));
    if (value[0] == 2) {
        requests.erase(
            std::remove_if(requests.begin(), requests.end(), [id](const Request &r) { return r.id == id; }),
            requests.end());
        if (active && current.id == id)
            canceled = true;
        messages::Message removed;
        removed.id = id;
        apply_message_change(messages::ChangeKind::remove, removed);
        if (car_ready)
            send_to_car(dashlink::Notification{messages::ChangeKind::remove, session, removed});
        return;
    }
    const bool preexisting = ancs::is_preexisting(value[1]);
    if (!car_ready && (preexisting || now() >= discover_until)) return;
    // ANCS marks notifications already present in Notification Center when the
    // bridge connects. Import those as silent MAP history rather than dropping them.
    auto kind = preexisting ? messages::ChangeKind::history_add :
                (value[0] == 0 ? messages::ChangeKind::add : messages::ChangeKind::update);
    for (auto &r : requests)
        if (r.id == id)
            return; // Coalesce pending updates.
    if (requests.size() >= 32) {
        ESP_LOGW(tag, "Notification queue full; dropping newest");
        return;
    }
    // If an add is already in flight, its subsequent change is an update.
    if (active && current.id == id)
        kind = messages::ChangeKind::update;
    requests.push_back({id, kind});
    request_next();
}
static void subscribe(uint16_t handle) {
    subscribing = handle;
    ESP_LOGI(tag, "ANCS subscribing: channel=%s handle=0x%04x",
             handle == data_handle ? "data" : "notifications", unsigned(handle));
    if (log_request("notification registration", esp_ble_gattc_register_for_notify(interface_id, peer, handle)) != ESP_OK)
        disconnect("Could not register ANCS subscription");
}
static void gap(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *p) {
    Guard guard;
    switch (event) {
    case ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT: {
        privacy_status = p->local_privacy_cmpl.status;
        ESP_LOGI(tag, "BLE privacy complete: status=0x%02x", unsigned(privacy_status));
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
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT: {
        const bool raw = event == ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT;
        int result = raw ? p->adv_data_raw_cmpl.status : p->scan_rsp_data_cmpl.status;
        if (raw)
            adv_data_status = result;
        else
            scan_data_status = result;
        ESP_LOGI(tag, "BLE %s configured: status=0x%02x pending=%d",
                 raw ? "advertisement (ANCS + generic HID discovery)" : "scan response (device name)",
                 unsigned(result), adv_pending - 1);
        if (--adv_pending == 0)
            ESP_ERROR_CHECK(start_advertising());
        break;
    }
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        adv_state = p->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS ? "active" : "start failed";
        ESP_LOGI(tag, "BLE advertising start complete: status=0x%02x state=%s",
                 unsigned(p->adv_start_cmpl.status), adv_state);
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (p->adv_stop_cmpl.status == ESP_BT_STATUS_SUCCESS)
            adv_state = "stopped";
        ESP_LOGI(tag, "BLE advertising stop complete: status=0x%02x state=%s",
                 unsigned(p->adv_stop_cmpl.status), adv_state);
        break;
    case ESP_GAP_BLE_SEC_REQ_EVT: {
        const bool allowed = pairing_allowed(Peer::phone) || known(p->ble_security.ble_req.bd_addr);
        ESP_LOGI(tag, "BLE security request: accepted=%d pairing_open=%d",
                 allowed, pairing_allowed(Peer::phone));
        log_request("security response", esp_ble_gap_security_rsp(p->ble_security.ble_req.bd_addr, allowed));
        break;
    }
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        ESP_LOGI(tag, "BLE authentication complete: success=%d reason=0x%02x auth_mode=0x%02x",
                 p->ble_security.auth_cmpl.success,
                 unsigned(p->ble_security.auth_cmpl.success ? 0 : p->ble_security.auth_cmpl.fail_reason),
                 unsigned(p->ble_security.auth_cmpl.auth_mode));
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
        ESP_LOGI(tag, "BLE GATT registration complete: status=0x%02x interface=%u",
                 unsigned(p->reg.status), unsigned(id));
        if (p->reg.status != ESP_GATT_OK) {
            ESP_LOGE(tag, "GATT registration failed");
            break;
        }
        interface_id = id;
        ESP_ERROR_CHECK(esp_ble_gap_set_device_name("Dash Messages"));
        ESP_ERROR_CHECK(log_request("privacy configuration", esp_ble_gap_config_local_privacy(true)));
        break;
    case ESP_GATTC_CONNECT_EVT: {
        ESP_LOGI(tag, "BLE connection event: id=%u role=%u address_type=%u already_linked=%d",
                 unsigned(p->connect.conn_id), unsigned(p->connect.link_role),
                 unsigned(p->connect.ble_addr_type), linked);
        // iOS initiates the ANCS accessory connection, so Board A is normally
        // the peripheral (link_role=1). The GATT server used by setup and the
        // ANCS client share that physical connection. Only an *additional*
        // connection is setup-only; excluding every peripheral-role link leaves
        // Dash Messages visibly connected on iOS but never starts ANCS.
        if (linked) {
            setup_connection = p->connect.conn_id;
            esp_ble_gatt_creat_conn_params_t setup_params = {};
            memcpy(setup_params.remote_bda, p->connect.remote_bda, 6);
            setup_params.remote_addr_type = p->connect.ble_addr_type;
            setup_params.own_addr_type = BLE_ADDR_TYPE_RPA_PUBLIC;
            setup_params.is_direct = true;
            log_request("setup GATT open", esp_ble_gattc_enh_open(interface_id, &setup_params));
            ESP_LOGI(tag, "BLE setup link connected; ANCS discovery not started");
            break;
        }
        adv_state = "connected";
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
        if (log_request("GATT open", esp_ble_gattc_enh_open(interface_id, &params)) != ESP_OK)
            disconnect("Could not open iPhone GATT connection");
        break;
    }
    case ESP_GATTC_OPEN_EVT: {
        ESP_LOGI(tag, "BLE GATT open complete: status=0x%02x id=%u mtu=%u",
                 unsigned(p->open.status), unsigned(p->open.conn_id), unsigned(p->open.mtu));
        if (p->open.conn_id == setup_connection) {
            ESP_LOGI(tag, "BLE setup GATT opened; ANCS encryption not requested");
            break;
        }
        if (p->open.status != ESP_GATT_OK) {
            disconnect("GATT open failed");
            break;
        }
        connection = p->open.conn_id;
        if (log_request("encryption", esp_ble_set_encryption(peer, ESP_BLE_SEC_ENCRYPT)) != ESP_OK) {
            disconnect("Could not encrypt iPhone link");
            break;
        }
        if (log_request("MTU exchange", esp_ble_gattc_send_mtu_req(interface_id, connection)) != ESP_OK) {
            mtu_ready = true;
            search();
        }
        break;
    }
    case ESP_GATTC_CFG_MTU_EVT:
        ESP_LOGI(tag, "BLE MTU complete: status=0x%02x mtu=%u",
                 unsigned(p->cfg_mtu.status), unsigned(p->cfg_mtu.mtu));
        mtu_ready = true;
        search();
        break;
    case ESP_GATTC_SEARCH_RES_EVT:
        if (p->search_res.srvc_id.uuid.len == ESP_UUID_LEN_128 &&
            !memcmp(p->search_res.srvc_id.uuid.uuid.uuid128, service_uuid, 16)) {
            start_handle = p->search_res.start_handle;
            end_handle = p->search_res.end_handle;
            ESP_LOGI(tag, "ANCS service found: handles=0x%04x-0x%04x",
                     unsigned(start_handle), unsigned(end_handle));
        }
        break;
    case ESP_GATTC_SEARCH_CMPL_EVT: {
        ESP_LOGI(tag, "ANCS discovery complete: status=0x%02x service_found=%d",
                 unsigned(p->search_cmpl.status), start_handle != 0);
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
        ESP_LOGI(tag, "ANCS characteristics: notifications=0x%04x data=0x%04x control=0x%04x",
                 unsigned(source_handle), unsigned(data_handle), unsigned(control_handle));
        if (!source_handle || !data_handle || !control_handle) {
            disconnect("Incomplete ANCS service");
            break;
        }
        subscribe(data_handle); // Details channel must be ready before new events arrive.
        break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
        ESP_LOGI(tag, "ANCS notification registration complete: status=0x%02x handle=0x%04x",
                 unsigned(p->reg_for_notify.status), unsigned(p->reg_for_notify.handle));
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
        if (log_request("subscription write", esp_ble_gattc_write_char_descr(interface_id, connection, descriptor.handle, 2, enable,
                                           ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE)) != ESP_OK)
            disconnect("ANCS subscription write failed");
        break;
    }
    case ESP_GATTC_WRITE_DESCR_EVT:
        ESP_LOGI(tag, "ANCS subscription complete: status=0x%02x handle=0x%04x",
                 unsigned(p->write.status), unsigned(p->write.handle));
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
            int result = 0;
            messages::Message notice;
            std::string value;
            if (fetch == Fetch::identity)
                result = identity_response.feed(p->notify.value, p->notify.value_len, current.id, value);
            else if (fetch == Fetch::app_name)
                result = name_response.feed(p->notify.value, p->notify.value_len, current_app, value);
            else if (fetch == Fetch::details)
                result = response.feed(p->notify.value, p->notify.value_len, current.id, notice);
            if (result < 0) {
                disconnect("Invalid notification response; reconnecting");
                break;
            }
            if (result == 1) {
                if (fetch == Fetch::identity) {
                    current_app = value;
                    remember(value);
                    auto *entry = seen(value);
                    if (!canceled && now() < discover_until && entry && !entry->named)
                        request_app_name();
                    else {
                        setup::ApplicationRule rule;
                        if (!canceled && car_ready && notification_rule(value, rule)) request_details();
                        else finish_request();
                    }
                } else if (fetch == Fetch::app_name) {
                    remember(current_app, value, true);
                    setup::ApplicationRule rule;
                    if (!canceled && car_ready && notification_rule(current_app, rule)) request_details();
                    else finish_request();
                } else {
                    setup::ApplicationRule rule;
                    if (!canceled && car_ready && notification_rule(current_app, rule) &&
                        notice.app == current_app) {
                        auto projected = ancs::apply_preview(std::move(notice), rule);
                        apply_message_change(current.kind, projected);
                        send_to_car(dashlink::Notification{current.kind, session, std::move(projected)});
                        ESP_LOGI(tag, "Allowed notification forwarded (content not logged)");
                    }
                    finish_request();
                }
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
        ESP_LOGI(tag, "BLE disconnected: id=%u reason=0x%02x",
                 unsigned(p->disconnect.conn_id), unsigned(p->disconnect.reason));
        if (p->disconnect.conn_id == setup_connection)
            setup_connection = UINT16_MAX;
        if (!linked || p->disconnect.conn_id != connection) {
            ESP_LOGI(tag, "BLE setup link disconnected; ANCS state unchanged");
            if (!linked) start_advertising();
            break;
        }
        phone_status();
        linked = secured = mtu_ready = searching = ready = active = false;
        start_handle = end_handle = source_handle = data_handle = control_handle = subscribing = 0;
        response.clear();
        identity_response.clear(); name_response.clear(); active = false; fetch = Fetch::none;
        new_session();
        ESP_LOGI(tag, "iPhone disconnected; inbox cleared");
        start_advertising();
        break;
    default:
        break;
    }
}
void phone_receive(const dashlink::Heartbeat &heartbeat) {
    if (heartbeat.source != dashlink::Board::car || heartbeat.session != 0)
        return;
    last_car = now();
    car_status = heartbeat.flags;
    bool available = (heartbeat.flags & 1) != 0;
    if (available != car_ready) {
        car_ready = available;
        new_session();
    }
}
void phone_poll() {
    if (now() - last_heartbeat >= 1000) {
        last_heartbeat = now();
        uint32_t flags = (phone_notifications_ready() ? 1 : 0) | (linked ? 4 : 0);
#if CONFIG_BRIDGE_CALL_RELAY
        if (relay_calls_ready()) flags |= 2;
#endif
        send_to_car(dashlink::Heartbeat{dashlink::Board::phone, session, flags});
    }
    if (car_ready && now() - last_car > 3000) {
        car_ready = false;
        car_status = 0;
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
void phone_status() {
    ESP_LOGI(tag, "BLE state: advertising=%s registered=%d privacy=%d adv_data=%d scan_data=%d pending=%d",
             adv_state, interface_id != ESP_GATT_IF_NONE, privacy_status, adv_data_status, scan_data_status, adv_pending);
    ESP_LOGI(tag, "BLE setup: linked=%d encrypted=%d mtu_ready=%d discovery_started=%d ancs_ready=%d bonds=%d",
             linked, secured, mtu_ready, searching, ready, esp_ble_get_bond_device_num());
}
bool phone_bluetooth_ready() { return linked; }
bool phone_board_link_ready() { return last_car && now() - last_car < 3000; }
bool phone_car_message_ready() { return car_ready && phone_board_link_ready(); }
bool phone_car_transport_ready() { return phone_board_link_ready() && (car_status & 4); }
bool phone_car_sync_ready() { return phone_board_link_ready() && (car_status & 8); }
bool phone_car_call_ready() { return phone_board_link_ready() && (car_status & 2); }
void phone_setup_apps() {
    std::string json = "{\"type\":\"apps\",\"role\":\"phone\",\"discovering\":" +
                       std::string(now() < discover_until ? "true" : "false") + ",\"allowed\":[";
    for (const auto &rule : setup_controller().application_rules()) {
        if (json.back() != '[') json += ',';
        json += "{\"id\":" + setup_escape(rule.id) + ",\"name\":" + setup_escape(rule.name) +
                ",\"preview\":" + std::to_string(unsigned(rule.preview)) + "}";
    }
    json += "],\"recent\":[";
    for (const auto &entry : seen_apps) {
        if (json.back() != '[') json += ',';
        json += "{\"id\":" + setup_escape(entry.id) + ",\"name\":" + setup_escape(entry.name) + "}";
    }
    setup_emit(json + "]}");
}
bool phone_setup_allow(const std::string &id, setup::Preview preview) {
    return setup_controller().allow_application(id, preview);
}
bool phone_setup_deny(const std::string &id) {
    return setup_controller().deny_application(id);
}
bool phone_setup_discover() {
    if (!phone_notifications_ready()) return false;
    discover_until = now() + 60000;
    return true;
}
bool phone_setup_test_notification() {
    if (!phone_notifications_ready() || !phone_car_message_ready() || !phone_car_sync_ready())
        return false;
    // Test-only, in RAM: never add the companion app to the saved allowlist.
    test_notification_until = now() + 120000;
    return true;
}
void phone_start() {
    load_policy();
    ESP_LOGI(tag, "BLE notification diagnostics enabled; configuration status -1=pending, 0=success");
    ESP_LOGI(tag, "BLE discovery compatibility test: ANCS + generic HID advertisement; no input reports");
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
    ESP_ERROR_CHECK(log_request("GATT registration", esp_ble_gattc_app_register(0)));
    new_session();
}
} // namespace runtime
#endif
