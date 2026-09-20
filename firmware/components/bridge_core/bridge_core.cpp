#include "bridge_core.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>

namespace bridge {
static uint16_t be16(const uint8_t *p) {
    return uint16_t(p[0]) * 256 + p[1];
}
static uint32_t le32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
static void put16(Bytes &b, size_t v) {
    b.push_back(v >> 8);
    b.push_back(v);
}
static void put32(Bytes &b, uint32_t v) {
    for (int i = 0; i < 4; i++)
        b.push_back(v >> (8 * i));
}
static uint32_t crc(const uint8_t *p, size_t n) {
    uint32_t c = ~0u;
    while (n--) {
        c ^= *p++;
        for (int i = 0; i < 8; i++)
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
}
static std::string clipped(const std::string &s, size_t max) {
    size_t n = std::min(s.size(), max);
    if (n < s.size())
        while (n && (uint8_t(s[n]) & 0xc0) == 0x80)
            --n;
    std::string out;
    out.reserve(n);
    for (size_t i = 0; i < n; i++)
        if (uint8_t(s[i]) >= 32 || s[i] == '\n' || s[i] == '\t')
            out += s[i];
    return out;
}
Notice bounded_notice(const Notice &notice) {
    return {notice.id, clipped(notice.app, 128), clipped(notice.title, 128),
            clipped(notice.subtitle, 128), clipped(notice.body, 768), clipped(notice.date, 32)};
}
Bytes encode(const WireMessage &m) {
    Bytes payload;
    put32(payload, m.session);
    put32(payload, m.notice.id);
    const auto notice = bounded_notice(m.notice);
    const std::array<std::string, 5> fields = {notice.app, notice.title, notice.subtitle, notice.body, notice.date};
    for (const auto &s : fields) {
        put16(payload, s.size());
        payload.insert(payload.end(), s.begin(), s.end());
    }
    Bytes out = {'T', 'W', 1, uint8_t(m.op)};
    put16(out, payload.size());
    out.insert(out.end(), payload.begin(), payload.end());
    put32(out, crc(out.data() + 2, out.size() - 2));
    return out;
}
void WireDecoder::feed(const uint8_t *p, size_t n, const std::function<void(const WireMessage &)> &receive) {
    for (size_t i = 0; i < n; i++) {
        data_.push_back(p[i]);
        while (data_.size() >= 2 && (data_[0] != 'T' || data_[1] != 'W'))
            data_.erase(data_.begin());
        if (data_.size() < 6)
            continue;
        size_t len = be16(data_.data() + 4), total = len + 10;
        if (data_[2] != 1 || data_[3] < 1 || data_[3] > 5 || len > 1302 || len < 18) {
            data_.erase(data_.begin());
            continue;
        }
        if (data_.size() < total)
            continue;
        if (crc(data_.data() + 2, total - 6) != le32(data_.data() + total - 4)) {
            data_.erase(data_.begin());
            continue;
        }
        WireMessage m{Op(data_[3]), le32(data_.data() + 6), {}};
        m.notice.id = le32(data_.data() + 10);
        std::array<std::string *, 5> fields = {&m.notice.app, &m.notice.title, &m.notice.subtitle,
                                               &m.notice.body, &m.notice.date};
        size_t pos = 14;
        bool ok = true;
        for (auto s : fields) {
            if (pos + 2 > total - 4) {
                ok = false;
                break;
            }
            size_t l = be16(data_.data() + pos);
            pos += 2;
            if (pos + l > total - 4) {
                ok = false;
                break;
            }
            s->assign(reinterpret_cast<char *>(data_.data() + pos), l);
            pos += l;
        }
        ok = ok && pos == total - 4;
        data_.erase(data_.begin(), data_.begin() + total);
        if (ok)
            receive(m);
    }
}
int AncsResponse::feed(const uint8_t *p, size_t n, uint32_t id, Notice &out) {
    if (data_.size() + n > 2048) {
        clear();
        return -1;
    }
    data_.insert(data_.end(), p, p + n);
    if (data_.size() < 5)
        return 0;
    if (data_[0] != 0 || le32(data_.data() + 1) != id) {
        clear();
        return -1;
    }
    Notice parsed;
    parsed.id = id;
    size_t pos = 5;
    uint8_t seen = 0;
    while (pos < data_.size()) {
        if (data_.size() - pos < 3)
            return 0;
        auto tag = data_[pos++];
        size_t len = data_[pos] | size_t(data_[pos + 1]) << 8;
        pos += 2;
        if (len > 1024) {
            clear();
            return -1;
        }
        if (data_.size() - pos < len)
            return 0;
        std::string value(reinterpret_cast<char *>(data_.data() + pos), len);
        pos += len;
        switch (tag) {
        case 0:
            parsed.app = value;
            seen |= 1;
            break;
        case 1:
            parsed.title = value;
            seen |= 2;
            break;
        case 2:
            parsed.subtitle = value;
            seen |= 4;
            break;
        case 3:
            parsed.body = value;
            seen |= 8;
            break;
        case 5:
            parsed.date = value;
            seen |= 16;
            break;
        default:
            clear();
            return -1;
        }
    }
    if (seen != 31)
        return 0;
    out = std::move(parsed);
    clear();
    return 1;
}
bool ObexFramer::feed(const uint8_t *p, size_t n, const std::function<void(const Bytes &)> &receive) {
    for (size_t i = 0; i < n; i++) {
        data_.push_back(p[i]);
        if (data_.size() < 3)
            continue;
        size_t len = be16(data_.data() + 1);
        if (len < 3 || len > max_packet) {
            clear();
            return false;
        }
        if (data_.size() == len) {
            Bytes packet;
            packet.swap(data_);
            receive(packet);
        }
    }
    return true;
}
Bytes obex_packet(uint8_t code, const Bytes &headers) {
    Bytes b = {code};
    put16(b, headers.size() + 3);
    b.insert(b.end(), headers.begin(), headers.end());
    return b;
}
void byte_header(Bytes &b, uint8_t tag, const Bytes &v) {
    b.push_back(tag);
    put16(b, v.size() + 3);
    b.insert(b.end(), v.begin(), v.end());
}
void uint_header(Bytes &b, uint8_t tag, uint32_t v) {
    b.push_back(tag);
    for (int i = 3; i >= 0; i--)
        b.push_back(v >> (8 * i));
}
static const Bytes mas_target = {0xbb, 0x58, 0x2b, 0x40, 0x42, 0x0c, 0x11, 0xdb,
                                 0xb0, 0xde, 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66};
static const Bytes mns_target = {0xbb, 0x58, 0x2b, 0x41, 0x42, 0x0c, 0x11, 0xdb,
                                 0xb0, 0xde, 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66};
Bytes mns_connect() {
    Bytes h = {0x10, 0, 0x04, 0};
    byte_header(h, 0x46, mns_target);
    return obex_packet(0x80, h);
}
std::string handle_text(uint64_t v) {
    char s[17];
    std::snprintf(s, sizeof s, "%016llX", static_cast<unsigned long long>(v));
    return s;
}
Bytes mns_event(uint32_t id, uint64_t handle) {
    Bytes h;
    if (id != UINT32_MAX)
        uint_header(h, 0xcb, id);
    std::string type = "x-bt/MAP-event-report";
    Bytes t(type.begin(), type.end());
    t.push_back(0);
    byte_header(h, 0x42, t);
    byte_header(h, 0x4c, {0x0f, 1, 0});
    std::string xml =
        "<?xml version=\"1.0\"?><MAP-event-report version=\"1.0\"><event type=\"NewMessage\" handle=\"" +
        handle_text(handle) + "\" folder=\"telecom/msg/inbox\" msg_type=\"SMS_GSM\"/></MAP-event-report>";
    byte_header(h, 0x49, Bytes(xml.begin(), xml.end()));
    return obex_packet(0x82, h);
}
struct Headers {
    std::map<uint8_t, Bytes> values;
    bool ok = true;
};
static Headers parse_headers(const Bytes &b, size_t pos = 0) {
    Headers h;
    while (pos < b.size()) {
        uint8_t tag = b[pos++];
        size_t len = 0;
        switch (tag >> 6) {
        case 0:
        case 1:
            if (pos + 2 > b.size()) {
                h.ok = false;
                return h;
            }
            len = be16(b.data() + pos);
            pos += 2;
            if (len < 3) {
                h.ok = false;
                return h;
            }
            len -= 3;
            break;
        case 2:
            len = 1;
            break;
        case 3:
            len = 4;
            break;
        }
        if (pos + len > b.size()) {
            h.ok = false;
            return h;
        }
        h.values[tag] = Bytes(b.begin() + pos, b.begin() + pos + len);
        pos += len;
    }
    return h;
}
bool obex_connection_id(const Bytes &b, uint32_t &id) {
    if (b.size() < 7 || b[0] != 0xa0 || be16(b.data() + 1) != b.size() || be16(b.data() + 5) < 255)
        return false;
    auto h = parse_headers(b, 7);
    auto it = h.values.find(0xcb);
    if (!h.ok)
        return false;
    // OBEX peers may omit Connection-ID for a single RFCOMM connection.
    if (it == h.values.end()) {
        id = UINT32_MAX;
        return true;
    }
    if (it->second.size() != 4)
        return false;
    id = 0;
    for (auto c : it->second)
        id = id * 256 + c;
    return true;
}
static std::string ascii_name(const Bytes &b) {
    if (b.size() % 2)
        return "!invalid";
    std::string s;
    for (size_t i = 0; i < b.size(); i += 2) {
        if (b[i] != 0)
            return "!invalid";
        if (b[i + 1] == 0) {
            if (i + 2 != b.size())
                return "!invalid";
            break;
        }
        s += char(b[i + 1]);
    }
    return s;
}
static std::string text(const Bytes &b) {
    return std::string(b.begin(), b.end() - (!b.empty() && b.back() == 0 ? 1 : 0));
}
static std::map<uint8_t, Bytes> app_params(const Bytes &b, bool &ok) {
    std::map<uint8_t, Bytes> r;
    size_t p = 0;
    while (p < b.size()) {
        if (p + 2 > b.size()) {
            ok = false;
            break;
        }
        uint8_t tag = b[p++], len = b[p++];
        if (p + len > b.size()) {
            ok = false;
            break;
        }
        r[tag] = Bytes(b.begin() + p, b.begin() + p + len);
        p += len;
    }
    return r;
}
static std::string xml_escape(const std::string &s) {
    std::string r;
    for (auto c : s) {
        switch (c) {
        case '&':
            r += "&amp;";
            break;
        case '<':
            r += "&lt;";
            break;
        case '>':
            r += "&gt;";
            break;
        case '"':
            r += "&quot;";
            break;
        case '\'':
            r += "&apos;";
            break;
        default:
            if (uint8_t(c) >= 32)
                r += c;
        }
    }
    return r;
}
void Inbox::clear() {
    messages_.clear();
}
uint64_t Inbox::apply(const WireMessage &m) {
    if (m.op == Op::heartbeat)
        return 0;
    if (m.op == Op::reset || m.session != session_) {
        clear();
        session_ = m.session;
    }
    if (m.op == Op::reset)
        return 0;
    auto it = std::find_if(messages_.begin(), messages_.end(),
                           [&](const Stored &s) { return s.notice.id == m.notice.id; });
    if (m.op == Op::remove) {
        if (it != messages_.end())
            messages_.erase(it);
        return 0;
    }
    if (m.notice.app != "net.whatsapp.WhatsApp" && m.notice.app != "net.whatsapp.WhatsAppSMB")
        return 0;
    Notice clean = m.notice;
    clean.title = clipped(clean.title, 128);
    clean.subtitle = clipped(clean.subtitle, 128);
    clean.body = clipped(clean.body, 768);
    if (it != messages_.end()) {
        it->notice = clean;
        return 0;
    }
    // Updates to notifications from before the trip must not resurrect old messages.
    if (m.op != Op::add)
        return 0;
    if (messages_.size() >= 32)
        messages_.erase(messages_.begin());
    const uint64_t handle = next_++;
    messages_.push_back({handle, clean, false});
    return handle;
}
Stored *Inbox::find(uint64_t h) {
    for (auto &s : messages_)
        if (s.handle == h)
            return &s;
    return nullptr;
}
bool Inbox::erase(uint64_t h) {
    auto it =
        std::find_if(messages_.begin(), messages_.end(), [&](const Stored &s) { return s.handle == h; });
    if (it == messages_.end())
        return false;
    messages_.erase(it);
    return true;
}
static bool parse_handle(const std::string &s, uint64_t &v) {
    if (s.empty() || s.size() > 16)
        return false;
    v = 0;
    for (auto c : s) {
        uint8_t d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else
            return false;
        v = v * 16 + d;
    }
    return true;
}
void MasServer::reset() {
    connected_ = false;
    notifications_ = false;
    folder_.clear();
    pending_headers_.clear();
    response_body_.clear();
    response_offset_ = 0;
    mtu_ = 1024;
}
Bytes MasServer::chunk(Bytes h) {
    const size_t overhead = 3 + h.size() + 3;
    if (overhead > mtu_)
        return obex_packet(0xd0);
    size_t n = std::min(size_t(mtu_) - overhead, response_body_.size() - response_offset_);
    bool final = response_offset_ + n == response_body_.size();
    byte_header(
        h, final ? 0x49 : 0x48,
        Bytes(response_body_.begin() + response_offset_, response_body_.begin() + response_offset_ + n));
    response_offset_ += n;
    if (final) {
        response_body_.clear();
        response_offset_ = 0;
    }
    return obex_packet(final ? 0xa0 : 0x90, h);
}
Bytes MasServer::get(const Bytes &raw) {
    auto h = parse_headers(raw);
    if (!h.ok)
        return obex_packet(0xc0);
    bool ok = true;
    auto params = app_params(h.values[0x4c], ok);
    if (!ok)
        return obex_packet(0xc0);
    auto word = [&](uint8_t tag, uint16_t def) {
        auto it = params.find(tag);
        if (it == params.end())
            return def;
        if (it->second.size() != 2) {
            ok = false;
            return def;
        }
        return be16(it->second.data());
    };
    uint16_t count = word(1, 1024), offset = word(2, 0);
    if (!ok)
        return obex_packet(0xc0);
    std::string type = text(h.values[0x42]), name = ascii_name(h.values[1]), body;
    Bytes headers;
    if (type == "x-obex/folder-listing") {
        std::vector<std::string> folders;
        if (folder_.empty())
            folders = {"telecom"};
        else if (folder_ == "telecom")
            folders = {"msg"};
        else if (folder_ == "telecom/msg")
            folders = {"inbox", "outbox", "sent", "deleted"};
        if (count == 0) {
            byte_header(headers, 0x4c, {0x11, 2, uint8_t(folders.size() >> 8), uint8_t(folders.size())});
            return obex_packet(0xa0, headers);
        }
        body = "<?xml version=\"1.0\"?><!DOCTYPE folder-listing SYSTEM "
               "\"obex-folder-listing.dtd\"><folder-listing version=\"1.0\">";
        for (size_t i = offset; i < folders.size() && i < size_t(offset) + count; i++)
            body += "<folder name=\"" + folders[i] + "\"/>";
        body += "</folder-listing>";
    } else if (type == "x-bt/MAP-msg-listing") {
        std::string path = name.empty() ? folder_ : folder_ + (folder_.empty() ? "" : "/") + name;
        if (path != "telecom/msg/inbox" && path != "telecom/msg/outbox" && path != "telecom/msg/sent" &&
            path != "telecom/msg/deleted")
            return obex_packet(0xc4);
        std::vector<const Stored *> list;
        if (path == "telecom/msg/inbox")
            for (auto it = inbox_.messages().rbegin(); it != inbox_.messages().rend(); ++it)
                list.push_back(&*it);
        if (params.count(3)) {
            if (params[3].size() != 1)
                return obex_packet(0xc0);
            if (params[3][0] & 1)
                list.clear();
        }
        if (params.count(6)) {
            if (params[6].size() != 1)
                return obex_packet(0xc0);
            uint8_t filter = params[6][0];
            list.erase(std::remove_if(list.begin(), list.end(),
                                      [&](const Stored *s) {
                                          return (filter == 1 && s->read) || (filter == 2 && !s->read);
                                      }),
                       list.end());
        }
        Bytes ap = {0x12, 2, uint8_t(list.size() >> 8), uint8_t(list.size()), 0x0d, 1, 0};
        byte_header(headers, 0x4c, ap);
        if (count == 0)
            return obex_packet(0xa0, headers);
        body = "<?xml version=\"1.0\"?><MAP-msg-listing version=\"1.0\">";
        for (size_t i = offset; i < list.size() && i < size_t(offset) + count; i++) {
            const auto &s = *list[i];
            const auto &n = s.notice;
            std::string date = n.date;
            if (date.size() != 15 || date[8] != 'T')
                date = "19700101T000000";
            body += "<msg handle=\"" + handle_text(s.handle) + "\" subject=\"" +
                    xml_escape(clipped(n.body, 64)) + "\" datetime=\"" + xml_escape(date) +
                    "\" sender_name=\"" + xml_escape(n.title) +
                    "\" sender_addressing=\"WhatsApp\" recipient_name=\"Driver\" recipient_addressing=\"\" "
                    "type=\"SMS_GSM\" size=\"" +
                    std::to_string(n.body.size()) +
                    "\" text=\"yes\" reception_status=\"complete\" attachment_size=\"0\" priority=\"no\" "
                    "read=\"" +
                    (s.read ? "yes" : "no") + "\" sent=\"no\" protected=\"no\"/>";
        }
        body += "</MAP-msg-listing>";
    } else if (type == "x-bt/message") {
        // Native SMS PDU encoding is deliberately unsupported; request UTF-8.
        if (params.count(0x14) && (params[0x14].size() != 1 || params[0x14][0] != 1))
            return obex_packet(0xc6);
        uint64_t handle;
        if (!parse_handle(name, handle))
            return obex_packet(0xc0);
        auto s = inbox_.find(handle);
        if (!s)
            return obex_packet(0xc4);
        auto one_line = [](std::string v) {
            for (auto &c : v)
                if (c == '\r' || c == '\n')
                    c = ' ';
            return v;
        };
        const auto &n = s->notice;
        std::string content = (n.subtitle.empty() ? "" : n.subtitle + "\n") + n.body;
        // Prevent a notification body from injecting bMessage delimiters.
        size_t p = 0;
        while ((p = content.find("END:MSG", p)) != std::string::npos) {
            content.replace(p, 7, "END MSG");
            p += 7;
        }
        std::string msg = "BEGIN:MSG\r\n" + content + "\r\nEND:MSG\r\n";
        body = "BEGIN:BMSG\r\nVERSION:1.0\r\nSTATUS:" + std::string(s->read ? "READ" : "UNREAD") +
               "\r\nTYPE:SMS_GSM\r\nFOLDER:telecom/msg/inbox\r\nBEGIN:VCARD\r\nVERSION:2.1\r\nN:" +
               one_line(n.title) + "\r\nFN:" + one_line(n.title) +
               "\r\nEND:VCARD\r\nBEGIN:BENV\r\nBEGIN:BBODY\r\nCHARSET:UTF-8\r\nLENGTH:" +
               std::to_string(msg.size()) + "\r\n" + msg + "END:BBODY\r\nEND:BENV\r\nEND:BMSG\r\n";
    } else
        return obex_packet(0xc4);
    response_body_ = Bytes(body.begin(), body.end());
    response_offset_ = 0;
    return chunk(headers);
}
Bytes MasServer::request(const Bytes &b) {
    if (b.size() < 3 || be16(b.data() + 1) != b.size() || b.size() > max_packet)
        return obex_packet(0xc0);
    if (b[0] == 0x80) {
        if (b.size() < 7 || be16(b.data() + 5) < 255)
            return obex_packet(0xc0);
        auto h = parse_headers(b, 7);
        if (!h.ok || h.values[0x46] != mas_target)
            return obex_packet(0xc6);
        reset();
        connected_ = true;
        mtu_ = std::min<uint16_t>(be16(b.data() + 5), 1024);
        Bytes r = {0x10, 0, 0x10, 0};
        byte_header(r, 0x4a, mas_target);
        uint_header(r, 0xcb, 1);
        return obex_packet(0xa0, r);
    }
    if (!connected_)
        return obex_packet(0xc1);
    if (b[0] == 0x81) {
        reset();
        return obex_packet(0xa0);
    }
    if (b[0] == 0xff) {
        response_body_.clear();
        response_offset_ = 0;
        pending_headers_.clear();
        return obex_packet(0xa0);
    }
    if (b[0] == 0x85) {
        if (b.size() < 5)
            return obex_packet(0xc0);
        auto h = parse_headers(b, 5);
        if (!h.ok)
            return obex_packet(0xc0);
        std::string name = ascii_name(h.values[1]), path = folder_;
        if (b[3] & 1) {
            auto pos = path.rfind('/');
            path = pos == std::string::npos ? "" : path.substr(0, pos);
        }
        if (name.empty() && !(b[3] & 1))
            path.clear();
        else if (!name.empty())
            path += (path.empty() ? "" : "/") + name;
        if (path != "" && path != "telecom" && path != "telecom/msg" && path != "telecom/msg/inbox" &&
            path != "telecom/msg/outbox" && path != "telecom/msg/sent" && path != "telecom/msg/deleted")
            return obex_packet(0xc4);
        folder_ = path;
        return obex_packet(0xa0);
    }
    if (b[0] == 0x03 || b[0] == 0x83) {
        if (!response_body_.empty())
            return chunk();
        if (pending_headers_.size() + b.size() - 3 > max_packet) {
            pending_headers_.clear();
            return obex_packet(0xcd);
        }
        pending_headers_.insert(pending_headers_.end(), b.begin() + 3, b.end());
        if (b[0] == 3)
            return obex_packet(0x90);
        Bytes h;
        h.swap(pending_headers_);
        return get(h);
    }
    if (b[0] == 0x82) {
        auto h = parse_headers(b, 3);
        if (!h.ok)
            return obex_packet(0xc0);
        bool ok = true;
        auto p = app_params(h.values[0x4c], ok);
        if (!ok)
            return obex_packet(0xc0);
        std::string type = text(h.values[0x42]);
        if (type == "x-bt/MAP-NotificationRegistration") {
            if (p[0x0e].size() != 1 || p[0x0e][0] > 1)
                return obex_packet(0xc0);
            notifications_ = p[0x0e][0];
            return obex_packet(0xa0);
        }
        if (type == "x-bt/messageStatus") {
            uint64_t id;
            if (!parse_handle(ascii_name(h.values[1]), id) || p[0x17].size() != 1 || p[0x18].size() != 1 ||
                p[0x18][0] > 1)
                return obex_packet(0xc0);
            auto s = inbox_.find(id);
            if (!s)
                return obex_packet(0xc4);
            if (p[0x17][0] == 0)
                s->read = p[0x18][0];
            else if (p[0x17][0] == 1 && p[0x18][0] == 1)
                inbox_.erase(id);
            else
                return obex_packet(0xc6);
            return obex_packet(0xa0);
        }
        if (type == "x-bt/MAP-messageUpdate")
            return obex_packet(0xa0);
        return obex_packet(0xc3); // No outgoing messages; never fake successful delivery.
    }
    return obex_packet(0xd1);
}
} // namespace bridge
