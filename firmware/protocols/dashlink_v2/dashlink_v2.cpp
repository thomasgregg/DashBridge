#include "dashbridge/protocols/dashlink_v2.hpp"
#include "dashbridge/core/text.hpp"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace dashbridge::protocols::dashlink_v2 {
namespace {

enum class Type : uint8_t {
    heartbeat = 1,
    notification = 2,
    call = 3,
    music_state = 4,
    music_command = 5,
    contact_reset = 6,
    contact_entry = 7,
    contact_done = 8,
    contact_ack = 9,
};

uint16_t get16(const uint8_t *data) { return uint16_t(data[0]) << 8 | data[1]; }
uint32_t get32(const uint8_t *data) {
    return uint32_t(data[0]) | uint32_t(data[1]) << 8 | uint32_t(data[2]) << 16 |
           uint32_t(data[3]) << 24;
}
void put16(Bytes &bytes, size_t value) {
    bytes.push_back(uint8_t(value >> 8));
    bytes.push_back(uint8_t(value));
}
void put32(Bytes &bytes, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8)
        bytes.push_back(uint8_t(value >> shift));
}
uint32_t crc32(const uint8_t *data, size_t size) {
    uint32_t crc = ~0u;
    while (size--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}
bool put_string(Bytes &bytes, const std::string &value, size_t maximum) {
    if (value.size() > maximum || value.size() > 0xffff)
        return false;
    put16(bytes, value.size());
    bytes.insert(bytes.end(), value.begin(), value.end());
    return true;
}
void put_clean_string(Bytes &bytes, const std::string &value, size_t maximum) {
    const auto clean = core::clean_text(value, maximum);
    put16(bytes, clean.size());
    bytes.insert(bytes.end(), clean.begin(), clean.end());
}
bool get_string(const Bytes &bytes, size_t &offset, std::string &value, size_t maximum) {
    if (offset + 2 > bytes.size())
        return false;
    const size_t size = get16(bytes.data() + offset);
    offset += 2;
    if (size > maximum || offset + size > bytes.size())
        return false;
    value.assign(reinterpret_cast<const char *>(bytes.data() + offset), size);
    offset += size;
    return true;
}
bool add_notification(Bytes &payload, const Notification &notification) {
    const auto kind = unsigned(notification.kind);
    if (kind > unsigned(core::messages::ChangeKind::history_add) || !notification.session)
        return false;
    payload.push_back(uint8_t(kind));
    put32(payload, notification.session);
    put32(payload, notification.message.id);
    put_clean_string(payload, notification.message.app, 128);
    put_clean_string(payload, notification.message.title, 128);
    put_clean_string(payload, notification.message.subtitle, 128);
    put_clean_string(payload, notification.message.body, 768);
    put_clean_string(payload, notification.message.date, 32);
    return true;
}
bool add_call(Bytes &payload, const Call &call) {
    if (!call.session || call.kind.empty())
        return false;
    put32(payload, call.session);
    put32(payload, call.sequence);
    return put_string(payload, call.kind, 32) && put_string(payload, call.payload, 768) &&
           put_string(payload, call.auxiliary, 128) && put_string(payload, call.destination, 64);
}
bool add_music_state(Bytes &payload, const core::music::State &state) {
    if (!core::music::valid(state))
        return false;
    put32(payload, state.session);
    put32(payload, state.revision);
    put32(payload, state.length_ms);
    put32(payload, state.position_ms);
    payload.push_back(state.playback);
    return put_string(payload, state.title, core::music::maximum_metadata_length) &&
           put_string(payload, state.artist, core::music::maximum_metadata_length) &&
           put_string(payload, state.album, core::music::maximum_metadata_length) &&
           put_string(payload, state.track, core::music::maximum_metadata_length) &&
           put_string(payload, state.track_count, core::music::maximum_metadata_length) &&
           put_string(payload, state.genre, core::music::maximum_metadata_length);
}
bool add_contact_entry(Bytes &payload, const ContactEntry &contact) {
    if (!contact.session || !contact.sequence ||
        unsigned(contact.entry.repository) > unsigned(core::contacts::Repository::combined))
        return false;
    put32(payload, contact.session);
    put32(payload, contact.sequence);
    payload.push_back(uint8_t(contact.entry.repository));
    return put_string(payload, contact.entry.name, 80) &&
           put_string(payload, contact.entry.phones, 128) &&
           put_string(payload, contact.entry.addresses, 640) &&
           put_string(payload, contact.entry.timestamp, 32);
}

bool decode_payload(Type type, const Bytes &payload, Packet &packet) {
    size_t offset = 0;
    switch (type) {
    case Type::heartbeat: {
        if (payload.size() != 9 || (payload[0] != 1 && payload[0] != 2)) return false;
        packet = Heartbeat{Board(payload[0]), get32(payload.data() + 1), get32(payload.data() + 5)};
        return true;
    }
    case Type::notification: {
        if (payload.size() < 9 || payload[0] > uint8_t(core::messages::ChangeKind::history_add))
            return false;
        Notification value;
        value.kind = core::messages::ChangeKind(payload[0]);
        value.session = get32(payload.data() + 1);
        value.message.id = get32(payload.data() + 5);
        offset = 9;
        if (!value.session || !get_string(payload, offset, value.message.app, 128) ||
            !get_string(payload, offset, value.message.title, 128) ||
            !get_string(payload, offset, value.message.subtitle, 128) ||
            !get_string(payload, offset, value.message.body, 768) ||
            !get_string(payload, offset, value.message.date, 32) || offset != payload.size())
            return false;
        packet = std::move(value);
        return true;
    }
    case Type::call: {
        if (payload.size() < 8) return false;
        Call value;
        value.session = get32(payload.data());
        value.sequence = get32(payload.data() + 4);
        offset = 8;
        if (!value.session || !get_string(payload, offset, value.kind, 32) || value.kind.empty() ||
            !get_string(payload, offset, value.payload, 768) ||
            !get_string(payload, offset, value.auxiliary, 128) ||
            !get_string(payload, offset, value.destination, 64) || offset != payload.size())
            return false;
        packet = std::move(value);
        return true;
    }
    case Type::music_state: {
        if (payload.size() < 17) return false;
        core::music::State state;
        state.session = get32(payload.data());
        state.revision = get32(payload.data() + 4);
        state.length_ms = get32(payload.data() + 8);
        state.position_ms = get32(payload.data() + 12);
        state.playback = payload[16];
        offset = 17;
        if (!get_string(payload, offset, state.title, core::music::maximum_metadata_length) ||
            !get_string(payload, offset, state.artist, core::music::maximum_metadata_length) ||
            !get_string(payload, offset, state.album, core::music::maximum_metadata_length) ||
            !get_string(payload, offset, state.track, core::music::maximum_metadata_length) ||
            !get_string(payload, offset, state.track_count, core::music::maximum_metadata_length) ||
            !get_string(payload, offset, state.genre, core::music::maximum_metadata_length) ||
            offset != payload.size() || !core::music::valid(state))
            return false;
        packet = MusicState{std::move(state)};
        return true;
    }
    case Type::music_command:
        if (payload.size() != 10 || !get32(payload.data()) || !get32(payload.data() + 4) ||
            payload[8] > 0x7f || payload[9] > 1)
            return false;
        packet = MusicCommand{get32(payload.data()), get32(payload.data() + 4), payload[8], payload[9]};
        return true;
    case Type::contact_reset:
        if (payload.size() != 4 || !get32(payload.data())) return false;
        packet = ContactReset{get32(payload.data())};
        return true;
    case Type::contact_entry: {
        if (payload.size() < 9 || !get32(payload.data()) || !get32(payload.data() + 4) ||
            payload[8] > uint8_t(core::contacts::Repository::combined))
            return false;
        ContactEntry value;
        value.session = get32(payload.data());
        value.sequence = get32(payload.data() + 4);
        value.entry.repository = core::contacts::Repository(payload[8]);
        offset = 9;
        if (!get_string(payload, offset, value.entry.name, 80) ||
            !get_string(payload, offset, value.entry.phones, 128) ||
            !get_string(payload, offset, value.entry.addresses, 640) ||
            !get_string(payload, offset, value.entry.timestamp, 32) || offset != payload.size())
            return false;
        packet = std::move(value);
        return true;
    }
    case Type::contact_done:
        if (payload.size() != 8 || !get32(payload.data())) return false;
        packet = ContactDone{get32(payload.data()), get32(payload.data() + 4)};
        return true;
    case Type::contact_ack:
        if (payload.size() != 9 || !get32(payload.data()) || payload[8] > 1) return false;
        packet = ContactAck{get32(payload.data()), get32(payload.data() + 4), bool(payload[8])};
        return true;
    }
    return false;
}

} // namespace

Bytes encode(const Packet &packet) {
    Bytes payload;
    Type type{};
    bool valid = std::visit([&](const auto &value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Heartbeat>) {
            type = Type::heartbeat;
            if ((value.source == Board::phone && !value.session) ||
                (value.source == Board::car && value.session)) return false;
            payload.push_back(uint8_t(value.source)); put32(payload, value.session); put32(payload, value.flags);
            return true;
        } else if constexpr (std::is_same_v<T, Notification>) {
            type = Type::notification; return add_notification(payload, value);
        } else if constexpr (std::is_same_v<T, Call>) {
            type = Type::call; return add_call(payload, value);
        } else if constexpr (std::is_same_v<T, MusicState>) {
            type = Type::music_state; return add_music_state(payload, value.state);
        } else if constexpr (std::is_same_v<T, MusicCommand>) {
            type = Type::music_command;
            if (!value.session || !value.sequence || value.key > 0x7f || value.state > 1) return false;
            put32(payload, value.session); put32(payload, value.sequence);
            payload.push_back(value.key); payload.push_back(value.state); return true;
        } else if constexpr (std::is_same_v<T, ContactReset>) {
            type = Type::contact_reset; if (!value.session) return false; put32(payload, value.session); return true;
        } else if constexpr (std::is_same_v<T, ContactEntry>) {
            type = Type::contact_entry; return add_contact_entry(payload, value);
        } else if constexpr (std::is_same_v<T, ContactDone>) {
            type = Type::contact_done; if (!value.session) return false;
            put32(payload, value.session); put32(payload, value.count); return true;
        } else {
            type = Type::contact_ack; if (!value.session) return false;
            put32(payload, value.session); put32(payload, value.count); payload.push_back(value.accepted); return true;
        }
    }, packet);
    if (!valid || payload.size() + 10 > maximum_frame_size)
        return {};
    Bytes result = {'D', 'L', 2, uint8_t(type)};
    put16(result, payload.size());
    result.insert(result.end(), payload.begin(), payload.end());
    put32(result, crc32(result.data() + 2, result.size() - 2));
    return result;
}

void Decoder::feed(const uint8_t *data, size_t size, const std::function<void(Packet)> &receive) {
    for (size_t index = 0; index < size; ++index) {
        data_.push_back(data[index]);
        while (data_.size() >= 2 && (data_[0] != 'D' || data_[1] != 'L'))
            data_.erase(data_.begin());
        if (data_.size() < 6)
            continue;
        const size_t payload_size = get16(data_.data() + 4);
        const size_t total = payload_size + 10;
        if (data_[2] != 2 || data_[3] < uint8_t(Type::heartbeat) ||
            data_[3] > uint8_t(Type::contact_ack) || total > maximum_frame_size) {
            ++malformed_;
            data_.erase(data_.begin());
            continue;
        }
        if (data_.size() < total)
            continue;
        if (crc32(data_.data() + 2, total - 6) != get32(data_.data() + total - 4)) {
            ++crc_failures_;
            data_.erase(data_.begin());
            continue;
        }
        Bytes payload(data_.begin() + 6, data_.begin() + total - 4);
        Packet packet;
        const bool valid = decode_payload(Type(data_[3]), payload, packet);
        data_.erase(data_.begin(), data_.begin() + total);
        if (valid)
            receive(std::move(packet));
        else
            ++malformed_;
    }
}

bool canonicalize(const Packet &input, Packet &output) {
    const auto frame = encode(input);
    if (frame.empty()) return false;
    bool received = false;
    Decoder decoder;
    decoder.feed(frame.data(), frame.size(), [&](Packet packet) {
        output = std::move(packet);
        received = true;
    });
    return received;
}

bool is_call(const Packet &packet) { return std::holds_alternative<Call>(packet); }
bool is_phone_heartbeat(const Packet &packet) {
    const auto heartbeat = std::get_if<Heartbeat>(&packet);
    return heartbeat && heartbeat->source == Board::phone && heartbeat->session;
}
bool is_car_heartbeat(const Packet &packet) {
    const auto heartbeat = std::get_if<Heartbeat>(&packet);
    return heartbeat && heartbeat->source == Board::car && !heartbeat->session;
}
bool supersedes_notifications(const Packet &packet) {
    const auto notification = std::get_if<Notification>(&packet);
    return notification && notification->kind == core::messages::ChangeKind::reset;
}

} // namespace dashbridge::protocols::dashlink_v2
