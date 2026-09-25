#include "dashbridge/protocols/setup_gatt_v1.hpp"
#include <utility>

namespace dashbridge::protocols::setup_gatt_v1 {

std::array<uint8_t, status_size> encode_status(const core::setup::Status &status) {
    uint16_t flags = 0;
    if (status.phone_bluetooth) flags |= 1u << 0;
    if (status.notifications) flags |= 1u << 1;
    if (status.phone_calls) flags |= 1u << 2;
    if (status.internal_link) flags |= 1u << 3;
    if (status.tesla_messages) flags |= 1u << 4;
    if (status.tesla_transport) flags |= 1u << 5;
    if (status.tesla_sync) flags |= 1u << 6;
    if (status.tesla_calls) flags |= 1u << 7;
    if (status.phone_pairing_open) flags |= 1u << 8;
    return {status_version, uint8_t(flags), uint8_t(flags >> 8)};
}

std::string encode_policy(const std::vector<std::string> &application_ids) {
    std::string encoded;
    for (const auto &id : application_ids) {
        encoded += id;
        encoded += '\n';
    }
    return encoded;
}

DecodedCommand decode_command(const uint8_t *data, size_t size) {
    if (!data || size < 1 || size > maximum_command_size)
        return {DecodeResult::invalid_pdu, {}};

    const uint8_t operation = data[0];
    if (operation == 1 || operation == 2) {
        std::string id(reinterpret_cast<const char *>(data + 1), size - 1);
        if (!core::setup::valid_application_id(id))
            return {DecodeResult::invalid_pdu, {}};
        const auto kind = operation == 1 ? core::setup::CommandKind::allow_application
                                         : core::setup::CommandKind::deny_application;
        return {DecodeResult::decoded, {kind, std::move(id)}};
    }

    if (size != 1) return {DecodeResult::invalid_pdu, {}};
    if (operation == 3)
        return {DecodeResult::decoded, {core::setup::CommandKind::open_phone_pairing, {}}};
    if (operation == 4)
        return {DecodeResult::decoded, {core::setup::CommandKind::test_notification, {}}};
    return {DecodeResult::unsupported, {}};
}

} // namespace dashbridge::protocols::setup_gatt_v1
