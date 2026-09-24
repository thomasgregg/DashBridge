#include "dashbridge/adapters/pbap_adapter.hpp"

#include <algorithm>

namespace dashbridge::adapters::car::pbap {
namespace contacts = dashbridge::core::contacts;
namespace obex = dashbridge::protocols::obex;

static const Bytes pbap_target = {0x79, 0x61, 0x35, 0xf0, 0xf0, 0xc5, 0x11, 0xd8,
                                  0x09, 0x66, 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66};

static std::string vcard_escape(const std::string &value) {
    std::string result;
    result.reserve(value.size() + 8);
    for (char character : value) {
        if (character == '\\' || character == ';' || character == ',')
            result += '\\';
        if (character == '\r' || character == '\n')
            result += "\\n";
        else if (uint8_t(character) >= 32)
            result += character;
    }
    return result;
}

static std::string xml_escape(const std::string &value) {
    std::string result;
    for (const auto character : value) {
        switch (character) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&apos;"; break;
        default:
            if (uint8_t(character) >= 32)
                result += character;
        }
    }
    return result;
}

static std::string lower_ascii(std::string value) {
    for (char &character : value)
        if (character >= 'A' && character <= 'Z')
            character = char(character - 'A' + 'a');
    return value;
}

static std::string contact_vcard(const contacts::Store &book, size_t index, uint8_t format) {
    const auto contact = book.at(index);
    const auto name = vcard_escape(contact.name);
    std::string result = "BEGIN:VCARD\r\nVERSION:" + std::string(format ? "3.0" : "2.1") +
                         "\r\nN:" + name + ";;;;\r\nFN:" + name + "\r\n";
    size_t at = 0;
    while (at < contact.phones.size()) {
        size_t end = contact.phones.find('\n', at);
        if (end == std::string::npos)
            end = contact.phones.size();
        const size_t tab = contact.phones.find('\t', at);
        if (tab != std::string::npos && tab < end)
            result += "TEL;TYPE=" + contact.phones.substr(at, tab - at) + ":" +
                      vcard_escape(contact.phones.substr(tab + 1, end - tab - 1)) + "\r\n";
        at = end + 1;
    }
    at = 0;
    while (at < contact.addresses.size()) {
        size_t end = contact.addresses.find('\n', at);
        if (end == std::string::npos)
            end = contact.addresses.size();
        result += "ADR;TYPE=OTHER:;;" + vcard_escape(contact.addresses.substr(at, end - at)) +
                  ";;;;\r\n";
        at = end + 1;
    }
    if (!contact.timestamp.empty()) {
        const char *kind = contact.repository == contacts::Repository::missed
                               ? "MISSED"
                               : contact.repository == contacts::Repository::outgoing ? "DIALED" : "RECEIVED";
        result += "X-IRMC-CALL-DATETIME;TYPE=" + std::string(kind) + ":" +
                  vcard_escape(contact.timestamp) + "\r\n";
    }
    return result + "END:VCARD\r\n";
}

void Server::reset() {
    connected_ = false;
    response_active_ = false;
    mtu_ = 1024;
    folder_.clear();
    prefix_.clear();
    suffix_.clear();
    fragment_.clear();
    selected_.clear();
    pending_headers_.clear();
    prefix_at_ = suffix_at_ = fragment_at_ = selected_at_ = 0;
    response_kind_ = format_ = 0;
}

void Server::begin_response(uint8_t kind, std::vector<uint16_t> selected, uint8_t format,
                            std::string prefix, std::string suffix) {
    response_kind_ = kind;
    selected_ = std::move(selected);
    format_ = format;
    prefix_ = std::move(prefix);
    suffix_ = std::move(suffix);
    fragment_.clear();
    prefix_at_ = suffix_at_ = fragment_at_ = selected_at_ = 0;
    response_active_ = true;
}

bool Server::append_body(Bytes &body, size_t capacity) {
    auto append = [&](const std::string &source, size_t &at) {
        const size_t count = std::min(capacity - body.size(), source.size() - at);
        body.insert(body.end(), source.begin() + at, source.begin() + at + count);
        at += count;
    };
    while (body.size() < capacity) {
        if (prefix_at_ < prefix_.size()) {
            append(prefix_, prefix_at_);
            continue;
        }
        if (fragment_at_ < fragment_.size()) {
            append(fragment_, fragment_at_);
            continue;
        }
        if (selected_at_ < selected_.size()) {
            const size_t index = selected_[selected_at_++];
            if (response_kind_ == 1) {
                fragment_ = contact_vcard(phonebook_, index, format_);
            } else {
                const auto contact = phonebook_.at(index);
                fragment_ = "<card handle=\"" + std::to_string(index + 1) +
                            ".vcf\" name=\"" + xml_escape(contact.name) + "\"/>";
            }
            fragment_at_ = 0;
            continue;
        }
        if (suffix_at_ < suffix_.size()) {
            append(suffix_, suffix_at_);
            continue;
        }
        break;
    }
    return prefix_at_ == prefix_.size() && fragment_at_ == fragment_.size() &&
           selected_at_ == selected_.size() && suffix_at_ == suffix_.size();
}

Bytes Server::chunk(Bytes headers) {
    const size_t overhead = 3 + headers.size() + 3;
    if (overhead > mtu_) {
        response_active_ = false;
        return obex::packet(0xd0);
    }
    Bytes body;
    const bool final = append_body(body, size_t(mtu_) - overhead);
    obex::byte_header(headers, final ? 0x49 : 0x48, body);
    if (final) {
        response_active_ = false;
        prefix_.clear();
        suffix_.clear();
        fragment_.clear();
        selected_.clear();
    }
    return obex::packet(final ? 0xa0 : 0x90, headers);
}

static bool repository_path(std::string path, contacts::Repository &repository) {
    while (!path.empty() && path.front() == '/')
        path.erase(path.begin());
    if (path.rfind("telecom/", 0) == 0)
        path.erase(0, 8);
    if (path.size() > 4 && path.substr(path.size() - 4) == ".vcf")
        path.resize(path.size() - 4);
    if (path == "pb") repository = contacts::Repository::contacts;
    else if (path == "fav") repository = contacts::Repository::favorites;
    else if (path == "ich") repository = contacts::Repository::incoming;
    else if (path == "och") repository = contacts::Repository::outgoing;
    else if (path == "mch") repository = contacts::Repository::missed;
    else if (path == "cch") repository = contacts::Repository::combined;
    else return false;
    return true;
}

Bytes Server::get(const Bytes &raw) {
    if (!phonebook_.ready())
        return obex::packet(0xd3);
    auto headers_in = obex::parse_headers(raw);
    if (!headers_in.ok)
        return obex::packet(0xc0);
    bool ok = true;
    const auto parameters = obex::parse_application_parameters(headers_in.values[0x4c], ok);
    auto word = [&](uint8_t tag, uint16_t fallback) {
        const auto item = parameters.find(tag);
        if (item == parameters.end())
            return fallback;
        if (item->second.size() != 2) {
            ok = false;
            return fallback;
        }
        return obex::read_be16(item->second.data());
    };
    const uint16_t count = word(0x04, 0xffff), offset = word(0x05, 0);
    uint8_t format = 0;
    const auto format_parameter = parameters.find(0x07);
    if (format_parameter != parameters.end()) {
        if (format_parameter->second.size() != 1 || format_parameter->second[0] > 1)
            ok = false;
        else
            format = format_parameter->second[0];
    }
    if (!ok)
        return obex::packet(0xc0);
    const std::string type = obex::text(headers_in.values[0x42]);
    const std::string name = obex::ascii_name(headers_in.values[1]);
    const std::string path = name.empty() ? folder_ : name;
    contacts::Repository repository = contacts::Repository::contacts;
    if (type == "x-bt/vcard") {
        if (!repository_path(folder_, repository))
            return obex::packet(0xc4);
    } else if (!repository_path(path, repository)) {
        return obex::packet(0xc4);
    }
    std::vector<uint16_t> selected;
    selected.reserve(phonebook_.size());
    for (size_t index = 0; index < phonebook_.size(); ++index)
        if (phonebook_.at(index).repository == repository)
            selected.push_back(uint16_t(index));
    const auto search = parameters.find(0x02);
    if (type == "x-bt/vcard-listing" && search != parameters.end()) {
        const std::string query = lower_ascii(obex::text(search->second));
        uint8_t property = 0;
        const auto property_parameter = parameters.find(0x03);
        if (property_parameter != parameters.end()) {
            if (property_parameter->second.size() != 1 || property_parameter->second[0] > 2)
                return obex::packet(0xc0);
            property = property_parameter->second[0];
        }
        selected.erase(std::remove_if(selected.begin(), selected.end(), [&](uint16_t index) {
                           const auto contact = phonebook_.at(index);
                           return lower_ascii(property == 1 ? contact.phones : contact.name).find(query) ==
                                  std::string::npos;
                       }),
                       selected.end());
    }
    const auto order = parameters.find(0x01);
    if (type == "x-bt/vcard-listing" && order != parameters.end()) {
        if (order->second.size() != 1 || order->second[0] > 1)
            return obex::packet(0xc0);
        if (order->second[0] == 1)
            std::sort(selected.begin(), selected.end(), [&](uint16_t left, uint16_t right) {
                return lower_ascii(phonebook_.at(left).name) < lower_ascii(phonebook_.at(right).name);
            });
    }
    const size_t total = selected.size();
    Bytes headers;
    if (count == 0) {
        obex::byte_header(headers, 0x4c,
                          {0x08, 2, uint8_t(total >> 8), uint8_t(total)});
        return obex::packet(0xa0, headers);
    }
    if (offset >= selected.size()) {
        selected.clear();
    } else {
        selected.erase(selected.begin(), selected.begin() + offset);
        if (selected.size() > count)
            selected.resize(count);
    }
    if (type == "x-bt/phonebook") {
        begin_response(1, std::move(selected), format);
    } else if (type == "x-bt/vcard-listing") {
        begin_response(2, std::move(selected), format,
                       "<?xml version=\"1.0\"?><!DOCTYPE vcard-listing SYSTEM \"vcard-listing.dtd\">"
                       "<vCard-listing version=\"1.0\">",
                       "</vCard-listing>");
    } else if (type == "x-bt/vcard") {
        const auto dot = name.find(".vcf");
        if (dot == std::string::npos || dot + 4 != name.size())
            return obex::packet(0xc0);
        uint64_t handle = 0;
        for (size_t index = 0; index < dot; ++index) {
            if (name[index] < '0' || name[index] > '9' || handle > 100000)
                return obex::packet(0xc0);
            handle = handle * 10 + unsigned(name[index] - '0');
        }
        if (dot == 0 || handle < 1 || handle > phonebook_.size() ||
            phonebook_.at(handle - 1).repository != repository)
            return obex::packet(0xc4);
        begin_response(1, {uint16_t(handle - 1)}, format);
    } else {
        return obex::packet(0xc4);
    }
    return chunk(headers);
}

Bytes Server::request(const Bytes &packet) {
    if (packet.size() < 3 || obex::read_be16(packet.data() + 1) != packet.size() ||
        packet.size() > obex::max_packet)
        return obex::packet(0xc0);
    if (packet[0] == 0x80) {
        if (!accepts_connect(packet))
            return obex::packet(0xc6);
        reset();
        connected_ = true;
        mtu_ = std::min<uint16_t>(obex::read_be16(packet.data() + 5), 1024);
        Bytes response = {0x10, 0, 0x10, 0};
        obex::byte_header(response, 0x4a, pbap_target);
        obex::uint_header(response, 0xcb, 2);
        return obex::packet(0xa0, response);
    }
    if (!connected_)
        return obex::packet(0xc1);
    if (packet[0] == 0x81) {
        reset();
        return obex::packet(0xa0);
    }
    if (packet[0] == 0xff) {
        response_active_ = false;
        pending_headers_.clear();
        return obex::packet(0xa0);
    }
    if (packet[0] == 0x85) {
        if (packet.size() < 5)
            return obex::packet(0xc0);
        auto headers = obex::parse_headers(packet, 5);
        if (!headers.ok)
            return obex::packet(0xc0);
        const std::string name = obex::ascii_name(headers.values[1]);
        std::string path = folder_;
        if (packet[3] & 1) {
            const auto slash = path.rfind('/');
            path = slash == std::string::npos ? "" : path.substr(0, slash);
        }
        if (name.empty() && !(packet[3] & 1))
            path.clear();
        else if (!name.empty())
            path += (path.empty() ? "" : "/") + name;
        contacts::Repository repository;
        if (path != "" && path != "telecom" && !repository_path(path, repository))
            return obex::packet(0xc4);
        folder_ = path;
        return obex::packet(0xa0);
    }
    if (packet[0] == 0x03 || packet[0] == 0x83) {
        if (response_active_)
            return chunk();
        if (pending_headers_.size() + packet.size() - 3 > obex::max_packet) {
            pending_headers_.clear();
            return obex::packet(0xcd);
        }
        pending_headers_.insert(pending_headers_.end(), packet.begin() + 3, packet.end());
        if (packet[0] == 0x03)
            return obex::packet(0x90);
        Bytes headers;
        headers.swap(pending_headers_);
        return get(headers);
    }
    return obex::packet(0xd1);
}

bool Server::accepts_connect(const Bytes &packet) {
    if (packet.size() < 7 || packet[0] != 0x80 ||
        obex::read_be16(packet.data() + 1) != packet.size() ||
        obex::read_be16(packet.data() + 5) < 255)
        return false;
    const auto headers = obex::parse_headers(packet, 7);
    const auto selected = headers.values.find(0x46);
    return headers.ok && selected != headers.values.end() && selected->second == pbap_target;
}

} // namespace dashbridge::adapters::car::pbap
