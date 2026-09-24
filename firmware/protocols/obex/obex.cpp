#include "dashbridge/protocols/obex.hpp"

namespace dashbridge::protocols::obex {

uint16_t read_be16(const uint8_t *data) {
    return uint16_t(data[0]) * 256 + data[1];
}

static void append_be16(Bytes &bytes, size_t value) {
    bytes.push_back(value >> 8);
    bytes.push_back(value);
}

Bytes packet(uint8_t code, const Bytes &headers) {
    Bytes result = {code};
    append_be16(result, headers.size() + 3);
    result.insert(result.end(), headers.begin(), headers.end());
    return result;
}

void byte_header(Bytes &to, uint8_t tag, const Bytes &value) {
    to.push_back(tag);
    append_be16(to, value.size() + 3);
    to.insert(to.end(), value.begin(), value.end());
}

void uint_header(Bytes &to, uint8_t tag, uint32_t value) {
    to.push_back(tag);
    for (int shift = 3; shift >= 0; --shift)
        to.push_back(value >> (8 * shift));
}

Headers parse_headers(const Bytes &bytes, size_t offset) {
    Headers headers;
    while (offset < bytes.size()) {
        const uint8_t tag = bytes[offset++];
        size_t size = 0;
        switch (tag >> 6) {
        case 0:
        case 1:
            if (offset + 2 > bytes.size()) {
                headers.ok = false;
                return headers;
            }
            size = read_be16(bytes.data() + offset);
            offset += 2;
            if (size < 3) {
                headers.ok = false;
                return headers;
            }
            size -= 3;
            break;
        case 2:
            size = 1;
            break;
        case 3:
            size = 4;
            break;
        }
        if (offset + size > bytes.size()) {
            headers.ok = false;
            return headers;
        }
        headers.values[tag] = Bytes(bytes.begin() + offset, bytes.begin() + offset + size);
        offset += size;
    }
    return headers;
}

std::map<uint8_t, Bytes> parse_application_parameters(const Bytes &value, bool &ok) {
    std::map<uint8_t, Bytes> result;
    size_t offset = 0;
    while (offset < value.size()) {
        if (offset + 2 > value.size()) {
            ok = false;
            break;
        }
        const uint8_t tag = value[offset++];
        const uint8_t size = value[offset++];
        if (offset + size > value.size()) {
            ok = false;
            break;
        }
        result[tag] = Bytes(value.begin() + offset, value.begin() + offset + size);
        offset += size;
    }
    return result;
}

std::string ascii_name(const Bytes &value) {
    if (value.size() % 2)
        return "!invalid";
    std::string result;
    for (size_t offset = 0; offset < value.size(); offset += 2) {
        if (value[offset] != 0)
            return "!invalid";
        if (value[offset + 1] == 0) {
            if (offset + 2 != value.size())
                return "!invalid";
            break;
        }
        result += char(value[offset + 1]);
    }
    return result;
}

std::string text(const Bytes &value) {
    return std::string(value.begin(), value.end() - (!value.empty() && value.back() == 0 ? 1 : 0));
}

bool Framer::feed(const uint8_t *data, size_t size,
                  const std::function<void(const Bytes &)> &receive) {
    for (size_t offset = 0; offset < size; ++offset) {
        data_.push_back(data[offset]);
        if (data_.size() < 3)
            continue;
        const size_t packet_size = read_be16(data_.data() + 1);
        if (packet_size < 3 || packet_size > max_packet) {
            clear();
            return false;
        }
        if (data_.size() == packet_size) {
            Bytes complete;
            complete.swap(data_);
            receive(complete);
        }
    }
    return true;
}

} // namespace dashbridge::protocols::obex
