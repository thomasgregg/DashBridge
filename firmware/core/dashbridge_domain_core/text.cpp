#include "dashbridge/core/text.hpp"

#include <algorithm>
#include <cstdint>

namespace dashbridge::core {

std::string clean_text(std::string_view value, size_t maximum) {
    size_t size = std::min(value.size(), maximum);
    if (size < value.size())
        while (size && (uint8_t(value[size]) & 0xc0) == 0x80)
            --size;
    std::string result;
    result.reserve(size);
    for (size_t index = 0; index < size; ++index)
        if (uint8_t(value[index]) >= 32 || value[index] == '\n' || value[index] == '\t')
            result += value[index];
    return result;
}

bool parse_uint32(std::string_view text, uint32_t &value) {
    if (text.empty() || text.size() > 10)
        return false;
    uint64_t parsed = 0;
    for (const auto character : text) {
        if (character < '0' || character > '9')
            return false;
        parsed = parsed * 10 + unsigned(character - '0');
        if (parsed > UINT32_MAX)
            return false;
    }
    value = uint32_t(parsed);
    return true;
}

} // namespace dashbridge::core
