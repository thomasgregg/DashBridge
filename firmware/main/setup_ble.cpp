#include "runtime.hpp"
#include "call_relay.hpp"
#include "sdkconfig.h"
#if CONFIG_BRIDGE_PHONE || CONFIG_BRIDGE_SINGLE
#include "esp_gatts_api.h"
#include "esp_log.h"
#include <algorithm>
#include <cstring>

namespace runtime {
namespace {
// Match ios-app/App/BridgeBluetooth.swift. ESP-IDF stores 128-bit UUIDs
// least-significant byte first.
uint8_t service_uuid[16] = {0xf0, 0xa6, 0x58, 0xc2, 0x28, 0x3d, 0xc1, 0xa0,
                            0x16, 0x4e, 0xb1, 0xce, 0x3d, 0x6b, 0x9b, 0x0d};
uint8_t status_uuid[16] = {0xf1, 0xa6, 0x58, 0xc2, 0x28, 0x3d, 0xc1, 0xa0,
                           0x16, 0x4e, 0xb1, 0xce, 0x3d, 0x6b, 0x9b, 0x0d};
uint8_t policy_uuid[16] = {0xf2, 0xa6, 0x58, 0xc2, 0x28, 0x3d, 0xc1, 0xa0,
                           0x16, 0x4e, 0xb1, 0xce, 0x3d, 0x6b, 0x9b, 0x0d};
uint8_t command_uuid[16] = {0xf3, 0xa6, 0x58, 0xc2, 0x28, 0x3d, 0xc1, 0xa0,
                            0x16, 0x4e, 0xb1, 0xce, 0x3d, 0x6b, 0x9b, 0x0d};
uint16_t primary_uuid = ESP_GATT_UUID_PRI_SERVICE;
uint16_t declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
uint8_t read_property = ESP_GATT_CHAR_PROP_BIT_READ;
uint8_t write_property = ESP_GATT_CHAR_PROP_BIT_WRITE;

enum Attribute { service, status_decl, status_value, policy_decl, policy_value,
                 command_decl, command_value, count };
uint16_t handles[count] = {};
uint16_t mtu = 23;

const esp_gatts_attr_db_t attributes[count] = {
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, reinterpret_cast<uint8_t *>(&primary_uuid),
                           ESP_GATT_PERM_READ, 16, 16, service_uuid}},
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, reinterpret_cast<uint8_t *>(&declaration_uuid),
                           ESP_GATT_PERM_READ, 1, 1, &read_property}},
    {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, status_uuid,
                             ESP_GATT_PERM_READ, 3, 0, nullptr}},
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, reinterpret_cast<uint8_t *>(&declaration_uuid),
                           ESP_GATT_PERM_READ, 1, 1, &read_property}},
    {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, policy_uuid,
                             ESP_GATT_PERM_READ_ENCRYPTED, ESP_GATT_MAX_ATTR_LEN, 0, nullptr}},
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, reinterpret_cast<uint8_t *>(&declaration_uuid),
                           ESP_GATT_PERM_READ, 1, 1, &write_property}},
    {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, command_uuid,
                             ESP_GATT_PERM_WRITE_ENCRYPTED, 96, 0, nullptr}},
};

void respond_read(esp_gatt_if_t id, const esp_ble_gatts_cb_param_t *p) {
    std::string bytes;
    if (p->read.handle == handles[status_value]) {
        uint16_t flags = 0;
        if (phone_bluetooth_ready()) flags |= 1u << 0;
        if (phone_notifications_ready()) flags |= 1u << 1;
#if CONFIG_BRIDGE_CALL_RELAY
        if (relay_calls_ready()) flags |= 1u << 2;
#endif
        if (phone_board_link_ready()) flags |= 1u << 3;
        if (phone_car_message_ready()) flags |= 1u << 4;
        if (phone_car_transport_ready()) flags |= 1u << 5;
        if (phone_car_sync_ready()) flags |= 1u << 6;
        if (phone_car_call_ready()) flags |= 1u << 7;
        if (pairing_allowed(Peer::phone)) flags |= 1u << 8;
        bytes = {char(1), char(flags & 0xff), char(flags >> 8)};
    } else if (p->read.handle == handles[policy_value]) {
        bytes = phone_setup_policy_ids();
    } else {
        esp_ble_gatts_send_response(id, p->read.conn_id, p->read.trans_id,
                                    ESP_GATT_READ_NOT_PERMIT, nullptr);
        return;
    }
    esp_gatt_rsp_t response = {};
    response.attr_value.handle = p->read.handle;
    response.attr_value.offset = p->read.offset;
    esp_gatt_status_t result = ESP_GATT_OK;
    if (p->read.offset > bytes.size()) result = ESP_GATT_INVALID_OFFSET;
    else {
        size_t n = std::min<size_t>(bytes.size() - p->read.offset,
                                    std::min<size_t>(mtu - 1, ESP_GATT_MAX_ATTR_LEN));
        response.attr_value.len = uint16_t(n);
        if (n) std::memcpy(response.attr_value.value, bytes.data() + p->read.offset, n);
    }
    esp_ble_gatts_send_response(id, p->read.conn_id, p->read.trans_id, result, &response);
}

esp_gatt_status_t apply_command(const uint8_t *data, size_t size) {
    if (size < 1 || size > 96) return ESP_GATT_INVALID_PDU;
    uint8_t op = data[0];
    if (op == 1 || op == 2) {
        std::string app(reinterpret_cast<const char *>(data + 1), size - 1);
        if (!bridge::valid_app_id(app)) return ESP_GATT_INVALID_PDU;
        bool ok = op == 1 ? phone_setup_allow(app, bridge::Preview::full)
                          : phone_setup_deny(app);
        return ok ? ESP_GATT_OK : ESP_GATT_ERROR;
    }
    if (size != 1) return ESP_GATT_INVALID_PDU;
    if (op == 3) {
        setup_open_pairing(Peer::phone);
        return ESP_GATT_OK;
    }
    if (op == 4) return phone_setup_test_notification() ? ESP_GATT_OK : ESP_GATT_ERROR;
    // Tesla pairing and test messages require a matching Board B protocol
    // update. Never claim success while Board B runs the existing 0.4.2 image.
    return ESP_GATT_ERROR;
}

void callback(esp_gatts_cb_event_t event, esp_gatt_if_t id, esp_ble_gatts_cb_param_t *p) {
    Guard guard;
    switch (event) {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI("setup_ble", "GATT server registration: status=0x%02x", unsigned(p->reg.status));
        if (p->reg.status == ESP_GATT_OK)
            esp_ble_gatts_create_attr_tab(attributes, id, count, 0);
        break;
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (p->add_attr_tab.status == ESP_GATT_OK && p->add_attr_tab.num_handle == count) {
            std::memcpy(handles, p->add_attr_tab.handles, sizeof handles);
            esp_ble_gatts_start_service(handles[service]);
            ESP_LOGI("setup_ble", "Setup service created");
        } else ESP_LOGE("setup_ble", "Could not create setup service");
        break;
    case ESP_GATTS_START_EVT:
        ESP_LOGI("setup_ble", "Setup service started: status=0x%02x", unsigned(p->start.status));
        break;
    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI("setup_ble", "Setup link connected: id=%u role=%u",
                 unsigned(p->connect.conn_id), unsigned(p->connect.link_role));
        break;
    case ESP_GATTS_MTU_EVT:
        mtu = p->mtu.mtu;
        break;
    case ESP_GATTS_READ_EVT:
        ESP_LOGI("setup_ble", "Setup value read: handle=0x%04x", unsigned(p->read.handle));
        if (p->read.need_rsp) respond_read(id, p);
        break;
    case ESP_GATTS_WRITE_EVT:
        if (p->write.need_rsp) {
            auto result = p->write.handle == handles[command_value] && !p->write.is_prep && !p->write.offset
                              ? apply_command(p->write.value, p->write.len) : ESP_GATT_INVALID_PDU;
            esp_ble_gatts_send_response(id, p->write.conn_id, p->write.trans_id, result, nullptr);
        }
        break;
    case ESP_GATTS_DISCONNECT_EVT:
        mtu = 23;
        break;
    default:
        break;
    }
}
} // namespace

void setup_ble_start() {
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(callback));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(1));
}
} // namespace runtime
#endif
