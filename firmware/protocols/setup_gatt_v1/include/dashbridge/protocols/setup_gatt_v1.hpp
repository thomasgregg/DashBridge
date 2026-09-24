#pragma once

#include "dashbridge/core/setup.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dashbridge::protocols::setup_gatt_v1 {

inline constexpr uint8_t status_version = 1;
inline constexpr size_t status_size = 3;
inline constexpr size_t maximum_command_size = 96;

// ESP-IDF represents 128-bit GATT UUIDs least-significant byte first.
inline constexpr std::array<uint8_t, 16> service_uuid = {
    0xf0, 0xa6, 0x58, 0xc2, 0x28, 0x3d, 0xc1, 0xa0,
    0x16, 0x4e, 0xb1, 0xce, 0x3d, 0x6b, 0x9b, 0x0d,
};
inline constexpr std::array<uint8_t, 16> status_uuid = {
    0xf1, 0xa6, 0x58, 0xc2, 0x28, 0x3d, 0xc1, 0xa0,
    0x16, 0x4e, 0xb1, 0xce, 0x3d, 0x6b, 0x9b, 0x0d,
};
inline constexpr std::array<uint8_t, 16> policy_uuid = {
    0xf2, 0xa6, 0x58, 0xc2, 0x28, 0x3d, 0xc1, 0xa0,
    0x16, 0x4e, 0xb1, 0xce, 0x3d, 0x6b, 0x9b, 0x0d,
};
inline constexpr std::array<uint8_t, 16> command_uuid = {
    0xf3, 0xa6, 0x58, 0xc2, 0x28, 0x3d, 0xc1, 0xa0,
    0x16, 0x4e, 0xb1, 0xce, 0x3d, 0x6b, 0x9b, 0x0d,
};

enum class DecodeResult : uint8_t {
    decoded,
    invalid_pdu,
    unsupported,
};

struct DecodedCommand {
    DecodeResult result = DecodeResult::invalid_pdu;
    core::setup::Command command;
};

std::array<uint8_t, status_size> encode_status(const core::setup::Status &status);
std::string encode_policy(const std::vector<std::string> &application_ids);
DecodedCommand decode_command(const uint8_t *data, size_t size);

} // namespace dashbridge::protocols::setup_gatt_v1
