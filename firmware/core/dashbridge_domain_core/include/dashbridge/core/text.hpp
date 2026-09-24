#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace dashbridge::core {

// Bounds user-visible UTF-8 without splitting a continuation sequence and
// removes control bytes that are unsafe in Bluetooth and setup projections.
std::string clean_text(std::string_view value, size_t maximum);
bool parse_uint32(std::string_view text, uint32_t &value);

} // namespace dashbridge::core
