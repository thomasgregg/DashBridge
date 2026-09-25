#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace dashbridge::protocols::obex {

using Bytes = std::vector<uint8_t>;
constexpr size_t max_packet = 4096;

uint16_t read_be16(const uint8_t *data);
Bytes packet(uint8_t code, const Bytes &headers = {});
void byte_header(Bytes &to, uint8_t tag, const Bytes &value);
void uint_header(Bytes &to, uint8_t tag, uint32_t value);

struct Headers {
    std::map<uint8_t, Bytes> values;
    bool ok = true;
};

Headers parse_headers(const Bytes &packet, size_t offset = 0);
std::map<uint8_t, Bytes> parse_application_parameters(const Bytes &value, bool &ok);
std::string ascii_name(const Bytes &value);
std::string text(const Bytes &value);

class Framer {
    Bytes data_;

  public:
    bool feed(const uint8_t *data, size_t size,
              const std::function<void(const Bytes &)> &receive);
    void clear() { data_.clear(); }
};

} // namespace dashbridge::protocols::obex
