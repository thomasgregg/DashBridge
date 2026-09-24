#include "dashbridge/adapters/map_adapter.hpp"
#include "dashbridge/core/text.hpp"

#include <algorithm>
#include <cstdio>

namespace dashbridge::adapters::car::map {
namespace obex = dashbridge::protocols::obex;

static const Bytes mas_target = {0xbb, 0x58, 0x2b, 0x40, 0x42, 0x0c, 0x11, 0xdb,
                                 0xb0, 0xde, 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66};
static const Bytes mns_target = {0xbb, 0x58, 0x2b, 0x41, 0x42, 0x0c, 0x11, 0xdb,
                                 0xb0, 0xde, 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66};

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

static bool parse_handle(const std::string &text, uint64_t &value) {
    if (text.empty() || text.size() > 16)
        return false;
    value = 0;
    for (const auto character : text) {
        uint8_t digit;
        if (character >= '0' && character <= '9') digit = character - '0';
        else if (character >= 'A' && character <= 'F') digit = character - 'A' + 10;
        else if (character >= 'a' && character <= 'f') digit = character - 'a' + 10;
        else return false;
        value = value * 16 + digit;
    }
    return true;
}

bool accepts_connect(const Bytes &packet) {
    if (packet.size() < 7 || packet[0] != 0x80 ||
        obex::read_be16(packet.data() + 1) != packet.size() ||
        obex::read_be16(packet.data() + 5) < 255)
        return false;
    const auto headers = obex::parse_headers(packet, 7);
    const auto target = headers.values.find(0x46);
    return headers.ok && target != headers.values.end() && target->second == mas_target;
}

std::string handle_text(uint64_t value) {
    char text[17];
    std::snprintf(text, sizeof text, "%016llX", static_cast<unsigned long long>(value));
    return text;
}

void Server::reset() {
    connected_ = false;
    notifications_ = false;
    folder_.clear();
    pending_headers_.clear();
    response_body_.clear();
    response_offset_ = 0;
    mtu_ = 1024;
}

Bytes Server::chunk(Bytes headers) {
    const size_t overhead = 3 + headers.size() + 3;
    if (overhead > mtu_)
        return obex::packet(0xd0);
    const size_t size = std::min(size_t(mtu_) - overhead, response_body_.size() - response_offset_);
    const bool final = response_offset_ + size == response_body_.size();
    obex::byte_header(headers, final ? 0x49 : 0x48,
                      Bytes(response_body_.begin() + response_offset_,
                            response_body_.begin() + response_offset_ + size));
    response_offset_ += size;
    if (final) {
        response_body_.clear();
        response_offset_ = 0;
    }
    return obex::packet(final ? 0xa0 : 0x90, headers);
}

Bytes Server::get(const Bytes &raw) {
    auto headers_in = obex::parse_headers(raw);
    if (!headers_in.ok)
        return obex::packet(0xc0);
    bool ok = true;
    const auto parameters = obex::parse_application_parameters(headers_in.values[0x4c], ok);
    if (!ok)
        return obex::packet(0xc0);
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
    const uint16_t count = word(1, 1024), offset = word(2, 0);
    if (!ok)
        return obex::packet(0xc0);
    const std::string type = obex::text(headers_in.values[0x42]);
    const std::string name = obex::ascii_name(headers_in.values[1]);
    std::string body;
    Bytes headers;
    if (type == "x-obex/folder-listing") {
        std::vector<std::string> folders;
        if (folder_.empty()) folders = {"telecom"};
        else if (folder_ == "telecom") folders = {"msg"};
        else if (folder_ == "telecom/msg") folders = {"inbox", "outbox", "sent", "deleted"};
        if (count == 0) {
            obex::byte_header(headers, 0x4c,
                              {0x11, 2, uint8_t(folders.size() >> 8), uint8_t(folders.size())});
            return obex::packet(0xa0, headers);
        }
        body = "<?xml version=\"1.0\"?><!DOCTYPE folder-listing SYSTEM "
               "\"obex-folder-listing.dtd\"><folder-listing version=\"1.0\">";
        for (size_t index = offset; index < folders.size() && index < size_t(offset) + count; ++index)
            body += "<folder name=\"" + folders[index] + "\"/>";
        body += "</folder-listing>";
    } else if (type == "x-bt/MAP-msg-listing") {
        const std::string path = name.empty() ? folder_ : folder_ + (folder_.empty() ? "" : "/") + name;
        if (path != "telecom/msg/inbox" && path != "telecom/msg/outbox" && path != "telecom/msg/sent" &&
            path != "telecom/msg/deleted")
            return obex::packet(0xc4);
        std::vector<const dashbridge::core::messages::StoredMessage *> list;
        if (path == "telecom/msg/inbox")
            for (auto item = inbox_.messages().rbegin(); item != inbox_.messages().rend(); ++item)
                list.push_back(&*item);
        if (parameters.count(3)) {
            if (parameters.at(3).size() != 1)
                return obex::packet(0xc0);
            if (parameters.at(3)[0] & 1)
                list.clear();
        }
        if (parameters.count(6)) {
            if (parameters.at(6).size() != 1)
                return obex::packet(0xc0);
            const uint8_t filter = parameters.at(6)[0];
            list.erase(std::remove_if(list.begin(), list.end(), [&](const auto *message) {
                           return (filter == 1 && message->read) || (filter == 2 && !message->read);
                       }), list.end());
        }
        obex::byte_header(headers, 0x4c,
                          {0x12, 2, uint8_t(list.size() >> 8), uint8_t(list.size()), 0x0d, 1, 0});
        if (count == 0)
            return obex::packet(0xa0, headers);
        body = "<?xml version=\"1.0\"?><MAP-msg-listing version=\"1.0\">";
        for (size_t index = offset; index < list.size() && index < size_t(offset) + count; ++index) {
            const auto &stored = *list[index];
            const auto &message = stored.message;
            std::string date = message.date;
            if (date.size() != 15 || date[8] != 'T')
                date = "19700101T000000";
            body += "<msg handle=\"" + handle_text(stored.handle) + "\" subject=\"" +
                    xml_escape(core::clean_text(message.body, 64)) + "\" datetime=\"" + xml_escape(date) +
                    "\" sender_name=\"" + xml_escape(message.title) + "\" sender_addressing=\"" +
                    xml_escape(core::clean_text(message.app, 48)) +
                    "\" recipient_name=\"Driver\" recipient_addressing=\"\" type=\"SMS_GSM\" size=\"" +
                    std::to_string(message.body.size()) +
                    "\" text=\"yes\" reception_status=\"complete\" attachment_size=\"0\" priority=\"no\" read=\"" +
                    (stored.read ? "yes" : "no") + "\" sent=\"no\" protected=\"no\"/>";
        }
        body += "</MAP-msg-listing>";
    } else if (type == "x-bt/message") {
        if (parameters.count(0x14) &&
            (parameters.at(0x14).size() != 1 || parameters.at(0x14)[0] != 1))
            return obex::packet(0xc6);
        uint64_t handle;
        if (!parse_handle(name, handle))
            return obex::packet(0xc0);
        const auto stored = inbox_.find(handle);
        if (!stored)
            return obex::packet(0xc4);
        auto one_line = [](std::string value) {
            for (auto &character : value)
                if (character == '\r' || character == '\n') character = ' ';
            return value;
        };
        const auto &message = stored->message;
        std::string content = (message.subtitle.empty() ? "" : message.subtitle + "\n") + message.body;
        size_t position = 0;
        while ((position = content.find("END:MSG", position)) != std::string::npos) {
            content.replace(position, 7, "END MSG");
            position += 7;
        }
        const std::string message_body = "BEGIN:MSG\r\n" + content + "\r\nEND:MSG\r\n";
        body = "BEGIN:BMSG\r\nVERSION:1.0\r\nSTATUS:" + std::string(stored->read ? "READ" : "UNREAD") +
               "\r\nTYPE:SMS_GSM\r\nFOLDER:telecom/msg/inbox\r\nBEGIN:VCARD\r\nVERSION:2.1\r\nN:" +
               one_line(message.title) + "\r\nFN:" + one_line(message.title) +
               "\r\nEND:VCARD\r\nBEGIN:BENV\r\nBEGIN:BBODY\r\nCHARSET:UTF-8\r\nLENGTH:" +
               std::to_string(message_body.size()) + "\r\n" + message_body +
               "END:BBODY\r\nEND:BENV\r\nEND:BMSG\r\n";
    } else {
        return obex::packet(0xc4);
    }
    response_body_ = Bytes(body.begin(), body.end());
    response_offset_ = 0;
    return chunk(headers);
}

Bytes Server::request(const Bytes &request) {
    if (request.size() < 3 || obex::read_be16(request.data() + 1) != request.size() ||
        request.size() > obex::max_packet)
        return obex::packet(0xc0);
    if (request[0] == 0x80) {
        if (!accepts_connect(request))
            return request.size() < 7 || obex::read_be16(request.data() + 5) < 255
                       ? obex::packet(0xc0) : obex::packet(0xc6);
        reset();
        connected_ = true;
        mtu_ = std::min<uint16_t>(obex::read_be16(request.data() + 5), 1024);
        Bytes response = {0x10, 0, 0x10, 0};
        obex::byte_header(response, 0x4a, mas_target);
        obex::uint_header(response, 0xcb, 1);
        return obex::packet(0xa0, response);
    }
    if (!connected_)
        return obex::packet(0xc1);
    if (request[0] == 0x81) {
        reset();
        return obex::packet(0xa0);
    }
    if (request[0] == 0xff) {
        response_body_.clear();
        response_offset_ = 0;
        pending_headers_.clear();
        return obex::packet(0xa0);
    }
    if (request[0] == 0x85) {
        if (request.size() < 5)
            return obex::packet(0xc0);
        auto headers = obex::parse_headers(request, 5);
        if (!headers.ok)
            return obex::packet(0xc0);
        const std::string name = obex::ascii_name(headers.values[1]);
        std::string path = folder_;
        if (request[3] & 1) {
            const auto position = path.rfind('/');
            path = position == std::string::npos ? "" : path.substr(0, position);
        }
        if (name.empty() && !(request[3] & 1)) path.clear();
        else if (!name.empty()) path += (path.empty() ? "" : "/") + name;
        if (path != "" && path != "telecom" && path != "telecom/msg" && path != "telecom/msg/inbox" &&
            path != "telecom/msg/outbox" && path != "telecom/msg/sent" && path != "telecom/msg/deleted")
            return obex::packet(0xc4);
        folder_ = path;
        return obex::packet(0xa0);
    }
    if (request[0] == 0x03 || request[0] == 0x83) {
        if (!response_body_.empty())
            return chunk();
        if (pending_headers_.size() + request.size() - 3 > obex::max_packet) {
            pending_headers_.clear();
            return obex::packet(0xcd);
        }
        pending_headers_.insert(pending_headers_.end(), request.begin() + 3, request.end());
        if (request[0] == 3)
            return obex::packet(0x90);
        Bytes headers;
        headers.swap(pending_headers_);
        return get(headers);
    }
    if (request[0] == 0x82) {
        auto headers = obex::parse_headers(request, 3);
        if (!headers.ok)
            return obex::packet(0xc0);
        bool ok = true;
        const auto parameters = obex::parse_application_parameters(headers.values[0x4c], ok);
        if (!ok)
            return obex::packet(0xc0);
        const std::string type = obex::text(headers.values[0x42]);
        if (type == "x-bt/MAP-NotificationRegistration") {
            const auto registration = parameters.find(0x0e);
            if (registration == parameters.end() || registration->second.size() != 1 ||
                registration->second[0] > 1)
                return obex::packet(0xc0);
            notifications_ = registration->second[0];
            return obex::packet(0xa0);
        }
        if (type == "x-bt/messageStatus") {
            uint64_t handle;
            const auto indicator = parameters.find(0x17), value = parameters.find(0x18);
            if (!parse_handle(obex::ascii_name(headers.values[1]), handle) ||
                indicator == parameters.end() || value == parameters.end() ||
                indicator->second.size() != 1 || value->second.size() != 1 || value->second[0] > 1)
                return obex::packet(0xc0);
            if (!inbox_.find(handle))
                return obex::packet(0xc4);
            if (indicator->second[0] == 0) inbox_.mark_read(handle, value->second[0]);
            else if (indicator->second[0] == 1 && value->second[0] == 1) inbox_.erase(handle);
            else return obex::packet(0xc6);
            return obex::packet(0xa0);
        }
        if (type == "x-bt/MAP-messageUpdate")
            return obex::packet(0xa0);
        return obex::packet(0xc3);
    }
    return obex::packet(0xd1);
}

Bytes notification_connect() {
    Bytes headers = {0x10, 0, 0x04, 0};
    obex::byte_header(headers, 0x46, mns_target);
    return obex::packet(0x80, headers);
}

Bytes notification_event(uint32_t connection_id, uint64_t handle) {
    Bytes headers;
    if (connection_id != UINT32_MAX)
        obex::uint_header(headers, 0xcb, connection_id);
    std::string type = "x-bt/MAP-event-report";
    Bytes type_header(type.begin(), type.end());
    type_header.push_back(0);
    obex::byte_header(headers, 0x42, type_header);
    obex::byte_header(headers, 0x4c, {0x0f, 1, 0});
    const std::string xml =
        "<?xml version=\"1.0\"?><MAP-event-report version=\"1.0\"><event type=\"NewMessage\" handle=\"" +
        handle_text(handle) + "\" folder=\"telecom/msg/inbox\" msg_type=\"SMS_GSM\"/></MAP-event-report>";
    obex::byte_header(headers, 0x49, Bytes(xml.begin(), xml.end()));
    return obex::packet(0x82, headers);
}

bool notification_connection_id(const Bytes &packet, uint32_t &connection_id) {
    if (packet.size() < 7 || packet[0] != 0xa0 ||
        obex::read_be16(packet.data() + 1) != packet.size() ||
        obex::read_be16(packet.data() + 5) < 255)
        return false;
    const auto headers = obex::parse_headers(packet, 7);
    if (!headers.ok)
        return false;
    const auto id = headers.values.find(0xcb);
    if (id == headers.values.end()) {
        connection_id = UINT32_MAX;
        return true;
    }
    if (id->second.size() != 4)
        return false;
    connection_id = 0;
    for (const auto byte : id->second)
        connection_id = connection_id * 256 + byte;
    return true;
}

} // namespace dashbridge::adapters::car::map
