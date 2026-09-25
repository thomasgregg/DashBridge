#include "dashbridge/adapters/ancs_codec.hpp"
#include "dashbridge/core/text.hpp"

#include <algorithm>
#include <utility>

namespace dashbridge::adapters::ancs {
namespace {

uint32_t little_endian_32(const uint8_t *data) {
    return uint32_t(data[0]) | uint32_t(data[1]) << 8 | uint32_t(data[2]) << 16 |
           uint32_t(data[3]) << 24;
}

} // namespace

core::messages::Message apply_preview(core::messages::Message message,
                                      const core::setup::ApplicationRule &rule) {
    message.app = rule.name;
    if (rule.preview == core::setup::Preview::sender) {
        message.subtitle.clear();
        message.body = "New notification";
    } else if (rule.preview == core::setup::Preview::app) {
        message.title = rule.name;
        message.subtitle.clear();
        message.body = "New notification";
    }
    return message;
}

int NotificationResponse::feed(const uint8_t *data, size_t size, uint32_t expected_id,
                               core::messages::Message &result) {
    if (data_.size() + size > 2048) {
        clear();
        return -1;
    }
    data_.insert(data_.end(), data, data + size);
    if (data_.size() < 5)
        return 0;
    if (data_[0] != 0 || little_endian_32(data_.data() + 1) != expected_id) {
        clear();
        return -1;
    }
    core::messages::Message parsed;
    parsed.id = expected_id;
    size_t position = 5;
    uint8_t seen = 0;
    while (position < data_.size()) {
        if (data_.size() - position < 3)
            return 0;
        const auto attribute = data_[position++];
        const size_t length = data_[position] | size_t(data_[position + 1]) << 8;
        position += 2;
        if (length > 1024) {
            clear();
            return -1;
        }
        if (data_.size() - position < length)
            return 0;
        std::string value(reinterpret_cast<char *>(data_.data() + position), length);
        position += length;
        switch (attribute) {
        case 0: parsed.app = std::move(value); seen |= 1; break;
        case 1: parsed.title = std::move(value); seen |= 2; break;
        case 2: parsed.subtitle = std::move(value); seen |= 4; break;
        case 3: parsed.body = std::move(value); seen |= 8; break;
        case 5: parsed.date = std::move(value); seen |= 16; break;
        default:
            clear();
            return -1;
        }
    }
    if (seen != 31)
        return 0;
    result = std::move(parsed);
    clear();
    return 1;
}

int ApplicationIdResponse::feed(const uint8_t *data, size_t size, uint32_t expected_id,
                                std::string &application_id) {
    if (data_.size() + size > 256) {
        clear();
        return -1;
    }
    data_.insert(data_.end(), data, data + size);
    if (data_.size() < 5)
        return 0;
    if (data_[0] != 0 || little_endian_32(data_.data() + 1) != expected_id) {
        clear();
        return -1;
    }
    if (data_.size() < 8)
        return 0;
    const size_t length = data_[6] | size_t(data_[7]) << 8;
    if (data_[5] != 0 || length > 95) {
        clear();
        return -1;
    }
    if (data_.size() < 8 + length)
        return 0;
    if (data_.size() != 8 + length) {
        clear();
        return -1;
    }
    std::string parsed(data_.begin() + 8, data_.end());
    clear();
    if (!core::setup::valid_application_id(parsed))
        return -1;
    application_id = std::move(parsed);
    return 1;
}

int ApplicationNameResponse::feed(const uint8_t *data, size_t size, const std::string &expected_id,
                                  std::string &name) {
    if (data_.size() + size > 256) {
        clear();
        return -1;
    }
    data_.insert(data_.end(), data, data + size);
    if (data_.empty())
        return 0;
    if (data_[0] != 1 || (data_.size() > expected_id.size() + 1 &&
        !std::equal(expected_id.begin(), expected_id.end(), data_.begin() + 1))) {
        clear();
        return -1;
    }
    if (data_.size() < expected_id.size() + 5)
        return 0;
    if (data_[expected_id.size() + 1] != 0 || data_[expected_id.size() + 2] != 0) {
        clear();
        return -1;
    }
    const size_t length = data_[expected_id.size() + 3] |
                          size_t(data_[expected_id.size() + 4]) << 8;
    if (length > 96) {
        clear();
        return -1;
    }
    if (data_.size() < expected_id.size() + 5 + length)
        return 0;
    if (data_.size() != expected_id.size() + 5 + length) {
        clear();
        return -1;
    }
    name = core::clean_text(
        std::string(data_.begin() + expected_id.size() + 5, data_.end()), 48);
    clear();
    return name.empty() ? -1 : 1;
}

} // namespace dashbridge::adapters::ancs
