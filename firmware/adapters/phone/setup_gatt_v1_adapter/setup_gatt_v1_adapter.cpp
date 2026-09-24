#include "dashbridge/adapters/setup_gatt_v1_adapter.hpp"
#include "dashbridge/protocols/setup_gatt_v1.hpp"
#include "esp_gatts_api.h"
#include "esp_log.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace dashbridge::adapters {
namespace {
namespace protocol = protocols::setup_gatt_v1;

core::setup::Port *setup_port = nullptr;
std::array<uint8_t, 16> service_uuid = protocol::service_uuid;
std::array<uint8_t, 16> status_uuid = protocol::status_uuid;
std::array<uint8_t, 16> policy_uuid = protocol::policy_uuid;
std::array<uint8_t, 16> command_uuid = protocol::command_uuid;
uint16_t primary_uuid = ESP_GATT_UUID_PRI_SERVICE;
uint16_t declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
uint8_t read_property = ESP_GATT_CHAR_PROP_BIT_READ;
uint8_t write_property = ESP_GATT_CHAR_PROP_BIT_WRITE;

enum Attribute {
    service,
    status_decl,
    status_value,
    policy_decl,
    policy_value,
    command_decl,
    command_value,
    count,
};

uint16_t handles[count] = {};
uint16_t mtu = 23;

const esp_gatts_attr_db_t attributes[count] = {
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, reinterpret_cast<uint8_t *>(&primary_uuid),
                           ESP_GATT_PERM_READ, 16, 16, service_uuid.data()}},
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, reinterpret_cast<uint8_t *>(&declaration_uuid),
                           ESP_GATT_PERM_READ, 1, 1, &read_property}},
    {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, status_uuid.data(), ESP_GATT_PERM_READ,
                             protocol::status_size, 0, nullptr}},
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, reinterpret_cast<uint8_t *>(&declaration_uuid),
                           ESP_GATT_PERM_READ, 1, 1, &read_property}},
    {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, policy_uuid.data(),
                             ESP_GATT_PERM_READ_ENCRYPTED, ESP_GATT_MAX_ATTR_LEN, 0, nullptr}},
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, reinterpret_cast<uint8_t *>(&declaration_uuid),
                           ESP_GATT_PERM_READ, 1, 1, &write_property}},
    {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, command_uuid.data(),
                             ESP_GATT_PERM_WRITE_ENCRYPTED, protocol::maximum_command_size,
                             0, nullptr}},
};

void send_error(esp_gatt_if_t interface, const esp_ble_gatts_cb_param_t *parameters,
                esp_gatt_status_t error) {
    esp_ble_gatts_send_response(interface, parameters->read.conn_id,
                                parameters->read.trans_id, error, nullptr);
}

void respond_read(esp_gatt_if_t interface, const esp_ble_gatts_cb_param_t *parameters) {
    if (!setup_port) {
        send_error(interface, parameters, ESP_GATT_ERROR);
        return;
    }

    std::string bytes;
    if (parameters->read.handle == handles[status_value]) {
        const auto encoded = protocol::encode_status(setup_port->status());
        bytes.assign(reinterpret_cast<const char *>(encoded.data()), encoded.size());
    } else if (parameters->read.handle == handles[policy_value]) {
        bytes = protocol::encode_policy(setup_port->allowed_application_ids());
    } else {
        send_error(interface, parameters, ESP_GATT_READ_NOT_PERMIT);
        return;
    }

    esp_gatt_rsp_t response = {};
    response.attr_value.handle = parameters->read.handle;
    response.attr_value.offset = parameters->read.offset;
    esp_gatt_status_t result = ESP_GATT_OK;
    if (parameters->read.offset > bytes.size()) {
        result = ESP_GATT_INVALID_OFFSET;
    } else {
        const size_t size = std::min<size_t>(
            bytes.size() - parameters->read.offset,
            std::min<size_t>(mtu - 1, ESP_GATT_MAX_ATTR_LEN));
        response.attr_value.len = uint16_t(size);
        if (size) {
            std::memcpy(response.attr_value.value,
                        bytes.data() + parameters->read.offset, size);
        }
    }
    esp_ble_gatts_send_response(interface, parameters->read.conn_id,
                                parameters->read.trans_id, result, &response);
}

esp_gatt_status_t apply_command(const uint8_t *data, size_t size) {
    if (!setup_port) return ESP_GATT_ERROR;
    const auto decoded = protocol::decode_command(data, size);
    if (decoded.result == protocol::DecodeResult::invalid_pdu)
        return ESP_GATT_INVALID_PDU;
    if (decoded.result == protocol::DecodeResult::unsupported)
        return ESP_GATT_ERROR;
    return setup_port->execute(decoded.command) == core::setup::CommandResult::applied
               ? ESP_GATT_OK
               : ESP_GATT_ERROR;
}

void callback(esp_gatts_cb_event_t event, esp_gatt_if_t interface,
              esp_ble_gatts_cb_param_t *parameters) {
    switch (event) {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI("setup_ble", "GATT server registration: status=0x%02x",
                 unsigned(parameters->reg.status));
        if (parameters->reg.status == ESP_GATT_OK)
            esp_ble_gatts_create_attr_tab(attributes, interface, count, 0);
        break;
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (parameters->add_attr_tab.status == ESP_GATT_OK &&
            parameters->add_attr_tab.num_handle == count) {
            std::memcpy(handles, parameters->add_attr_tab.handles, sizeof handles);
            esp_ble_gatts_start_service(handles[service]);
            ESP_LOGI("setup_ble", "Setup service created");
        } else {
            ESP_LOGE("setup_ble", "Could not create setup service");
        }
        break;
    case ESP_GATTS_START_EVT:
        ESP_LOGI("setup_ble", "Setup service started: status=0x%02x",
                 unsigned(parameters->start.status));
        break;
    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI("setup_ble", "Setup link connected: id=%u role=%u",
                 unsigned(parameters->connect.conn_id),
                 unsigned(parameters->connect.link_role));
        break;
    case ESP_GATTS_MTU_EVT:
        mtu = parameters->mtu.mtu;
        break;
    case ESP_GATTS_READ_EVT:
        ESP_LOGI("setup_ble", "Setup value read: handle=0x%04x",
                 unsigned(parameters->read.handle));
        if (parameters->read.need_rsp) respond_read(interface, parameters);
        break;
    case ESP_GATTS_WRITE_EVT:
        if (parameters->write.need_rsp) {
            const auto result = parameters->write.handle == handles[command_value] &&
                                        !parameters->write.is_prep && !parameters->write.offset
                                    ? apply_command(parameters->write.value,
                                                    parameters->write.len)
                                    : ESP_GATT_INVALID_PDU;
            esp_ble_gatts_send_response(interface, parameters->write.conn_id,
                                        parameters->write.trans_id, result, nullptr);
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

void start_setup_gatt_v1(core::setup::Port &port) {
    setup_port = &port;
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(callback));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(1));
}

} // namespace dashbridge::adapters
