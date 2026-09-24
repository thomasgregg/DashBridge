#include "dashbridge/adapters/ancs_codec.hpp"
#include "dashbridge/adapters/map_adapter.hpp"
#include "dashbridge/adapters/pbap_adapter.hpp"
#include "dashbridge/core/contacts.hpp"
#include "dashbridge/core/setup.hpp"
#include "dashbridge/protocols/dashlink_v2.hpp"
#include "dashbridge/protocols/obex.hpp"
#include "dashbridge/platform/console_commands.hpp"
#include "dashbridge/transport/dashlink_transport.hpp"
#include <cassert>
#include <iostream>
#include <random>
namespace ancs = dashbridge::adapters::ancs;
namespace messages = dashbridge::core::messages;
namespace setup = dashbridge::core::setup;
namespace map_adapter = dashbridge::adapters::car::map;
namespace pbap_adapter = dashbridge::adapters::car::pbap;
namespace contacts_core = dashbridge::core::contacts;
namespace dashlink = dashbridge::protocols::dashlink_v2;
namespace obex = dashbridge::protocols::obex;
using MasServer = map_adapter::Server;
using PbapServer = pbap_adapter::Server;
using Phonebook = contacts_core::Store;
using PhonebookRepository = contacts_core::Repository;
using ObexFramer = obex::Framer;
using map_adapter::handle_text;
using obex::byte_header;
using Bytes = obex::Bytes;
using Notice = messages::Message;
using AppPolicy = setup::ApplicationPolicy;
using Preview = setup::Preview;

static Bytes obex_packet(uint8_t code, const Bytes &headers = {}) {
    return obex::packet(code, headers);
}
static Bytes mns_event(uint32_t connection_id, uint64_t handle) {
    return map_adapter::notification_event(connection_id, handle);
}
static bool obex_connection_id(const Bytes &packet, uint32_t &connection_id) {
    return map_adapter::notification_connection_id(packet, connection_id);
}

static uint64_t apply(messages::Store &store, messages::ChangeKind kind, uint32_t session,
                      const Notice &notice = {}) {
    return store.apply({kind, session, notice});
}

static Notice example(uint32_t id = 42) {
    return {id,
            "net.whatsapp.WhatsApp",
            "Alice & Bob",
            "Team <chat>",
            "Hello 👋 from München",
            "20260920T120000"};
}
static Bytes name(const std::string &s) {
    Bytes b;
    for (auto c : s) {
        b.push_back(0);
        b.push_back(c);
    }
    b.push_back(0);
    b.push_back(0);
    return b;
}
static Bytes headers(const std::string &type, const std::string &n = "", const Bytes &params = {}) {
    Bytes b, t(type.begin(), type.end());
    t.push_back(0);
    byte_header(b, 0x42, t);
    if (!n.empty())
        byte_header(b, 1, name(n));
    if (!params.empty())
        byte_header(b, 0x4c, params);
    return b;
}
static void connect(MasServer &s, unsigned mtu = 255) {
    Bytes b = {0x10, 0, uint8_t(mtu >> 8), uint8_t(mtu)};
    byte_header(
        b, 0x46,
        {0xbb, 0x58, 0x2b, 0x40, 0x42, 0x0c, 0x11, 0xdb, 0xb0, 0xde, 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66});
    auto r = s.request(obex_packet(0x80, b));
    uint32_t id;
    assert(obex_connection_id(r, id) && id == 1);
}
static void folder(MasServer &s, const std::string &n) {
    Bytes b = {2, 0};
    byte_header(b, 1, name(n));
    assert(s.request(obex_packet(0x85, b))[0] == 0xa0);
}
template<class Server> static std::string get_body(Server &s, Bytes r, size_t mtu = 255) {
    std::string result;
    for (int count = 0;; ++count) {
        assert(count < 100 && r.size() <= mtu && r.size() >= 3);
        assert(r[0] == 0x90 || r[0] == 0xa0);
        for (size_t p = 3; p < r.size();) {
            uint8_t tag = r[p++];
            size_t size;
            if ((tag >> 6) < 2) {
                assert(p + 2 <= r.size());
                size = size_t(r[p]) * 256 + r[p + 1];
                p += 2;
                assert(size >= 3);
                size -= 3;
            } else
                size = (tag >> 6) == 2 ? 1 : 4;
            assert(p + size <= r.size());
            if (tag == 0x48 || tag == 0x49)
                result.append(r.begin() + p, r.begin() + p + size);
            p += size;
        }
        if (r[0] == 0xa0)
            return result;
        r = s.request(obex_packet(0x83));
    }
}
static void dashlink_test() {
    dashlink::Notification input{messages::ChangeKind::add, 1234, example()};
    auto packet = dashlink::encode(input);
    for (size_t cut = 0; cut <= packet.size(); ++cut) {
        dashlink::Decoder decoder;
        int received = 0;
        auto check = [&](dashlink::Packet packet) {
            ++received;
            const auto *message = std::get_if<dashlink::Notification>(&packet);
            assert(message && message->session == 1234 && message->kind == messages::ChangeKind::add);
            assert(message->message.id == 42 && message->message.body == input.message.body);
        };
        decoder.feed(packet.data(), cut, check);
        decoder.feed(packet.data() + cut, packet.size() - cut, check);
        assert(received == 1);
    }
    dashlink::Decoder decoder;
    int received = 0;
    auto count = [&](dashlink::Packet) { ++received; };
    auto corrupt = packet;
    corrupt[15] ^= 1;
    decoder.feed(corrupt.data(), corrupt.size(), count);
    assert(received == 0);
    decoder.feed(packet.data(), packet.size(), count);
    assert(received == 1);
    Notice long_text = example();
    long_text.body = std::string(767, 'a') + "👋";
    auto clipped = dashlink::encode(dashlink::Notification{messages::ChangeKind::add, 1, long_text});
    decoder.feed(clipped.data(), clipped.size(),
                 [&](dashlink::Packet packet) {
                     assert(std::get<dashlink::Notification>(packet).message.body == std::string(767, 'a'));
                 });
    assert(decoder.crc_failures() == 1);
    std::cout << "PASS typed DashLink fragmentation, checksum recovery, UTF-8 boundary\n";
}
static dashlink::Packet dashlink_roundtrip(const dashlink::Packet &input) {
    const auto frame = dashlink::encode(input);
    assert(!frame.empty() && frame.size() <= dashlink::maximum_frame_size);
    dashlink::Decoder decoder;
    dashlink::Packet output;
    unsigned received = 0;
    for (uint8_t byte : frame)
        decoder.feed(&byte, 1, [&](dashlink::Packet packet) {
            output = std::move(packet);
            ++received;
        });
    assert(received == 1 && decoder.crc_failures() == 0 && decoder.malformed() == 0);
    return output;
}
static void dashlink_types_test() {
    auto heartbeat = std::get<dashlink::Heartbeat>(dashlink_roundtrip(
        dashlink::Heartbeat{dashlink::Board::phone, 77, 0x1234}));
    assert(heartbeat.source == dashlink::Board::phone && heartbeat.session == 77 && heartbeat.flags == 0x1234);

    dashlink::Call call{77, 9, "dial", "+491234", "3", "88"};
    auto decoded_call = std::get<dashlink::Call>(dashlink_roundtrip(call));
    assert(decoded_call.kind == call.kind && decoded_call.payload == call.payload &&
           decoded_call.auxiliary == call.auxiliary && decoded_call.destination == call.destination);

    dashbridge::core::music::State state;
    state.session = 77; state.revision = 4; state.playback = 1;
    state.length_ms = 10000; state.position_ms = 2500;
    state.title = "Song"; state.artist = "Artist"; state.album = "Album";
    state.track = "2"; state.track_count = "9"; state.genre = "Jazz";
    auto decoded_music = std::get<dashlink::MusicState>(dashlink_roundtrip(dashlink::MusicState{state}));
    assert(decoded_music.state.session == 77 && decoded_music.state.title == "Song" &&
           decoded_music.state.position_ms == 2500);
    auto command = std::get<dashlink::MusicCommand>(dashlink_roundtrip(
        dashlink::MusicCommand{77, 5, 0x44, 1}));
    assert(command.session == 77 && command.sequence == 5 && command.key == 0x44 && command.state == 1);

    assert(std::get<dashlink::ContactReset>(dashlink_roundtrip(dashlink::ContactReset{77})).session == 77);
    contacts_core::Entry entry{contacts_core::Repository::favorites, "Alice", "CELL\t+49123",
                               "Street 1, Berlin", "20260924T120000"};
    auto decoded_entry = std::get<dashlink::ContactEntry>(dashlink_roundtrip(
        dashlink::ContactEntry{77, 1, entry}));
    assert(decoded_entry.sequence == 1 && decoded_entry.entry.repository == entry.repository &&
           decoded_entry.entry.name == entry.name && decoded_entry.entry.phones == entry.phones &&
           decoded_entry.entry.addresses == entry.addresses);
    assert(std::get<dashlink::ContactDone>(dashlink_roundtrip(dashlink::ContactDone{77, 1})).count == 1);
    auto ack = std::get<dashlink::ContactAck>(dashlink_roundtrip(dashlink::ContactAck{77, 1, true}));
    assert(ack.count == 1 && ack.accepted);

    assert(dashlink::encode(dashlink::Heartbeat{dashlink::Board::phone, 0, 0}).empty());
    assert(dashlink::encode(dashlink::Heartbeat{dashlink::Board::car, 1, 0}).empty());
    assert(dashlink::encode(dashlink::Notification{messages::ChangeKind::add, 0, example()}).empty());
    assert(dashlink::encode(dashlink::Call{77, 1, "", {}, {}, {}}).empty());
    assert(dashlink::encode(dashlink::MusicCommand{77, 0, 0x44, 1}).empty());
    assert(dashlink::encode(dashlink::MusicCommand{77, 1, 0x44, 2}).empty());
    assert(dashlink::encode(dashlink::ContactEntry{77, 0, entry}).empty());

    dashlink::Decoder decoder;
    unsigned received = 0;
    auto valid = dashlink::encode(dashlink::Heartbeat{dashlink::Board::car, 0, 1});
    auto invalid_version = valid; invalid_version[2] = 99;
    auto invalid_type = valid; invalid_type[3] = 99;
    const dashlink::Bytes oversized = {'D', 'L', 2, 1, 0xff, 0xff};
    auto accept = [&](dashlink::Packet) { ++received; };
    decoder.feed(invalid_version.data(), invalid_version.size(), accept);
    decoder.feed(invalid_type.data(), invalid_type.size(), accept);
    decoder.feed(oversized.data(), oversized.size(), accept);
    decoder.feed(valid.data(), valid.size(), accept);
    assert(received == 1 && decoder.malformed() >= 3);
    std::cout << "PASS all DashLink domains, invalid states, type/version bounds and resynchronization\n";
}
static void ancs_test() {
    Bytes p = {0, 42, 0, 0, 0};
    Notice n = example();
    auto attr = [&](uint8_t id, const std::string &v) {
        p.push_back(id);
        p.push_back(v.size());
        p.push_back(v.size() >> 8);
        p.insert(p.end(), v.begin(), v.end());
    };
    attr(0, n.app);
    attr(1, n.title);
    attr(2, n.subtitle);
    attr(3, n.body);
    attr(5, n.date);
    for (size_t cut = 0; cut < p.size(); ++cut) {
        ancs::NotificationResponse parser;
        Notice got;
        assert(parser.feed(p.data(), cut, 42, got) == 0);
        assert(parser.feed(p.data() + cut, p.size() - cut, 42, got) == 1);
        assert(got.app == n.app && got.body == n.body && got.date == n.date);
    }
    ancs::NotificationResponse parser;
    Notice got;
    assert(parser.feed(p.data(), p.size(), 99, got) == -1);
    Bytes too_big(2049, 0);
    assert(parser.feed(too_big.data(), too_big.size(), 42, got) == -1);
    Bytes bad = {0, 42, 0, 0, 0, 0, 0xff, 0xff};
    assert(parser.feed(bad.data(), bad.size(), 42, got) == -1);
    Bytes app_id = {0, 42, 0, 0, 0, 0, uint8_t(n.app.size()), 0};
    app_id.insert(app_id.end(), n.app.begin(), n.app.end());
    for (size_t cut = 0; cut < app_id.size(); ++cut) {
        ancs::ApplicationIdResponse identity;
        std::string id;
        assert(identity.feed(app_id.data(), cut, 42, id) == 0);
        assert(identity.feed(app_id.data() + cut, app_id.size() - cut, 42, id) == 1);
        assert(id == n.app);
    }
    ancs::ApplicationIdResponse identity;
    std::string id;
    assert(identity.feed(app_id.data(), app_id.size(), 43, id) == -1);
    Bytes app_name = {1};
    app_name.insert(app_name.end(), n.app.begin(), n.app.end());
    app_name.insert(app_name.end(), {0, 0, 8, 0});
    app_name.insert(app_name.end(), {'W', 'h', 'a', 't', 's', 'A', 'p', 'p'});
    for (size_t cut = 0; cut < app_name.size(); ++cut) {
        ancs::ApplicationNameResponse names;
        std::string label;
        assert(names.feed(app_name.data(), cut, n.app, label) == 0);
        assert(names.feed(app_name.data() + cut, app_name.size() - cut, n.app, label) == 1);
        assert(label == "WhatsApp");
    }
    std::cout << "PASS ANCS fragmented attributes, wrong UID and oversize rejection\n";
}
static void app_policy_test() {
    AppPolicy policy;
    assert(policy.rules().empty());
    assert(!policy.find("com.example.Chat"));
    assert(policy.allow("com.example.Chat", "Chat", Preview::sender));
    auto hidden = ancs::apply_preview(example(), {"net.whatsapp.WhatsApp", "WhatsApp", Preview::app});
    assert(hidden.title == "WhatsApp" && hidden.body == "New notification" && hidden.subtitle.empty());
    auto stored = policy.serialize();
    AppPolicy restored;
    assert(restored.load(stored));
    assert(restored.find("com.example.Chat") && restored.find("com.example.Chat")->preview == Preview::sender);
    stored.back() = 255;
    assert(!restored.load(stored));
    assert(restored.find("com.example.Chat"));
    assert(!policy.allow("com.example.Bad ID", "Bad", Preview::full));
    assert(policy.deny("com.example.Chat"));
    assert(!policy.find("com.example.Chat"));
    AppPolicy priorBoard;
    assert(priorBoard.allow("net.whatsapp.WhatsAppSMB", "WhatsApp Business", Preview::full));
    AppPolicy upgradedBoard;
    assert(upgradedBoard.load(priorBoard.serialize()));
    assert(upgradedBoard.find("net.whatsapp.WhatsAppSMB"));
    std::cout << "PASS empty app choice defaults, persistence, validation and preview privacy\n";
}
static void inbox_test() {
    messages::Store box;
    auto n = example();
    auto h = apply(box, messages::ChangeKind::add, 1, n);
    assert(h);
    assert(!apply(box, messages::ChangeKind::add, 1, n) && box.messages().size() == 1);
    n.body = "Edited";
    assert(!apply(box, messages::ChangeKind::update, 1, n));
    assert(box.find(h)->message.body == "Edited");
    n.id = 99;
    assert(!apply(box, messages::ChangeKind::update, 1, n));
    assert(box.messages().size() == 1);
    n = example(100);
    assert(!apply(box, messages::ChangeKind::history_add, 1, n));
    assert(box.messages().size() == 2 && box.messages().back().message.id == 100);
    n = example(101);
    n.app = "Signal";
    assert(apply(box, messages::ChangeKind::add, 1, n));
    apply(box, messages::ChangeKind::remove, 1, example());
    assert(box.messages().size() == 2 && box.messages()[0].message.id == 100);
    apply(box, messages::ChangeKind::add, 2, example());
    apply(box, messages::ChangeKind::reset, 3);
    assert(box.messages().empty());
    for (int i = 0; i < 40; ++i)
        apply(box, messages::ChangeKind::add, 3, example(i));
    assert(box.messages().size() == 32);
    std::cout << "PASS multi-app live/history, duplicate/update handling, session clearing and bounds\n";
}

static void pbap_connect(PbapServer &server, unsigned mtu = 255) {
    Bytes b = {0x10, 0, uint8_t(mtu >> 8), uint8_t(mtu)};
    byte_header(b, 0x46,
        {0x79, 0x61, 0x35, 0xf0, 0xf0, 0xc5, 0x11, 0xd8, 0x09, 0x66, 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66});
    assert(server.request(obex_packet(0x80, b))[0] == 0xa0);
}
static void pbap_folder(PbapServer &server, const std::string &value) {
    Bytes b = {2, 0};
    byte_header(b, 1, name(value));
    assert(server.request(obex_packet(0x85, b))[0] == 0xa0);
}
static void phonebook_test() {
    Phonebook book;
    assert(book.add({PhonebookRepository::contacts, "Jane Doe",
                     "CELL\t+491234\nWORK\t5550100", "Main Street 1\nOffice Road 2", ""}));
    assert(book.add({PhonebookRepository::favorites, "Jane Doe", "CELL\t+491234", {}, {}}));
    assert(book.add({PhonebookRepository::missed, "John Smith", "VOICE\t12345", {}, "20260922T101500"}));
    assert(book.add({PhonebookRepository::outgoing, "Alice", "CELL\t67890", {}, "20260922T111500"}));
    assert(book.size() == 4 && book.size(PhonebookRepository::contacts) == 1 &&
           book.size(PhonebookRepository::favorites) == 1);
    auto jane = book.at(0);
    assert(jane.phones.find("WORK\t5550100") != std::string::npos &&
           jane.addresses.find("Office Road 2") != std::string::npos);
    book.ready(true);
    PbapServer server(book);
    pbap_connect(server);
    auto contacts = get_body(server, server.request(obex_packet(
        0x83, headers("x-bt/phonebook", "telecom/pb.vcf", {7, 1, 1}))));
    assert(contacts.find("TEL;TYPE=CELL:+491234") != std::string::npos);
    assert(contacts.find("TEL;TYPE=WORK:5550100") != std::string::npos);
    assert(contacts.find("ADR;TYPE=OTHER:;;Main Street 1") != std::string::npos);
    auto missed = get_body(server, server.request(obex_packet(
        0x83, headers("x-bt/phonebook", "telecom/mch.vcf"))));
    assert(missed.find("John Smith") != std::string::npos &&
           missed.find("X-IRMC-CALL-DATETIME;TYPE=MISSED:20260922T101500") != std::string::npos);
    pbap_folder(server, "telecom");
    pbap_folder(server, "fav");
    auto favorites = get_body(server, server.request(obex_packet(0x83, headers("x-bt/vcard-listing"))));
    assert(favorites.find("Jane Doe") != std::string::npos);
    auto favorite = get_body(server, server.request(obex_packet(0x83, headers("x-bt/vcard", "2.vcf"))));
    assert(favorite.find("TEL;TYPE=CELL:+491234") != std::string::npos);
    std::cout << "PASS grouped contacts, addresses, favorites and call-history PBAP repositories\n";
}

static void contact_transfer_test() {
    using namespace contacts_core;
    const Entry jane{Repository::contacts, "Jane Doe", "CELL\t+491234", "Main Street 1", {}};
    const Entry alice{Repository::favorites, "Alice", "VOICE\t5550100", {}, {}};
    Store store;
    TransferReceiver receiver(store);
    assert(receiver.reset(10) == ReceiveResult::accepted);
    assert(receiver.entry(9, 1, jane) == ReceiveResult::ignored);
    assert(receiver.entry(10, 1, jane) == ReceiveResult::accepted);
    assert(receiver.entry(10, 2, jane) == ReceiveResult::accepted); // exact duplicate is idempotent
    assert(receiver.received() == 2 && store.size() == 0);
    assert(receiver.done(9, 1) == ReceiveResult::ignored);
    assert(receiver.done(10, 2) == ReceiveResult::completed); // duplicate is coalesced atomically
    assert(store.ready() && store.size() == 1 && receiver.pending_ack() &&
           receiver.pending_ack()->accepted);
    receiver.ack_sent();

    assert(receiver.reset(11) == ReceiveResult::accepted);
    assert(receiver.entry(11, 2, jane) == ReceiveResult::rejected); // missing first frame
    assert(receiver.entry(11, 1, jane) == ReceiveResult::rejected); // rejected session stays poisoned
    assert(receiver.done(11, 1) == ReceiveResult::rejected && store.size() == 1 && store.ready());

    assert(receiver.reset(12) == ReceiveResult::accepted);
    assert(receiver.entry(12, 1, jane) == ReceiveResult::accepted);
    assert(receiver.entry(12, 2, alice) == ReceiveResult::accepted);
    assert(receiver.done(12, 2) == ReceiveResult::completed);
    assert(store.ready() && store.size() == 2 && receiver.pending_ack()->accepted);
    assert(receiver.reset(0) == ReceiveResult::rejected && store.ready() && store.size() == 2);

    TransferSender sender;
    assert(!sender.start(0, true));
    assert(sender.start(20, false) && sender.step(2) == SendStep::reset);
    sender.sent(2);
    assert(sender.step(2) == SendStep::wait_source);
    sender.source_ready();
    assert(sender.step(2) == SendStep::entry && sender.next() == 0);
    sender.sent(2);
    sender.sent(2);
    assert(sender.step(2) == SendStep::done);
    sender.sent(2);
    assert(sender.step(2) == SendStep::wait_ack);
    assert(sender.accept({19, 2, true}, 2) == AckResult::ignored);
    assert(sender.accept({20, 1, true}, 2) == AckResult::retry &&
           sender.step(2) == SendStep::reset);
    sender.sent(2);
    sender.sent(2);
    sender.sent(2);
    sender.sent(2);
    assert(sender.accept({20, 2, true}, 2) == AckResult::complete &&
           sender.step(2) == SendStep::complete);
    std::cout << "PASS atomic contact transfer, stale sessions, gaps, retries and acknowledgements\n";
}
static void map_test() {
    messages::Store box;
    auto h = apply(box, messages::ChangeKind::add, 1, example());
    MasServer server(box);
    assert(server.request(obex_packet(0x83))[0] == 0xc1);
    connect(server);
    folder(server, "telecom");
    folder(server, "msg");
    auto listing =
        get_body(server, server.request(obex_packet(0x83, headers("x-bt/MAP-msg-listing", "inbox"))));
    assert(listing.find("Alice &amp; Bob") != std::string::npos);
    assert(listing.find("sender_addressing=\"net.whatsapp.WhatsApp\"") != std::string::npos);
    assert(listing.find(handle_text(h)) != std::string::npos);
    auto paged = get_body(
        server, server.request(obex_packet(0x83, headers("x-bt/MAP-msg-listing", "inbox", {2, 2, 0, 1}))));
    assert(paged.find("<msg ") == std::string::npos);
    auto bmsg = get_body(
        server, server.request(obex_packet(0x83, headers("x-bt/message", handle_text(h), {0x14, 1, 1}))));
    assert(bmsg.find(example().body) != std::string::npos);
    size_t begin = bmsg.find("BEGIN:MSG\r\n"), end = bmsg.find("END:MSG\r\n") + 9;
    assert(bmsg.find("LENGTH:" + std::to_string(end - begin) + "\r\n") != std::string::npos);
    assert(server.request(obex_packet(0x83, headers("x-bt/message", handle_text(h), {0x14, 1, 0})))[0] ==
           0xc6);
    assert(server.request(obex_packet(0x82, headers("x-bt/message", "outbox")))[0] == 0xc3);
    assert(server.request(
               obex_packet(0x82, headers("x-bt/MAP-NotificationRegistration", "", {0x0e, 1, 1})))[0] == 0xa0);
    assert(server.notifications());
    assert(server.request(obex_packet(
               0x82, headers("x-bt/messageStatus", handle_text(h), {0x17, 1, 0, 0x18, 1, 1})))[0] == 0xa0);
    assert(box.find(h)->read);
    server.request(obex_packet(0x81));
    assert(!server.notifications());
    std::cout << "PASS MAP listing, XML escaping, MTU continuation, bMessage byte length, local read status, "
                 "unsupported reply rejection\n";
}
static void framer_test() {
    uint32_t id = 0;
    assert(obex_connection_id({0xa0, 0, 7, 0x10, 0, 0, 255}, id) && id == UINT32_MAX);
    assert(!obex_connection_id({0xa0, 0, 7, 0x10, 0, 0, 254}, id));
    assert(!obex_connection_id({0xc0, 0, 7, 0x10, 0, 0, 255}, id));
    auto p = mns_event(1, 2);
    assert(p.size() <= 255);
    ObexFramer f;
    int received = 0;
    for (auto byte : p)
        assert(f.feed(&byte, 1, [&](const Bytes &got) {
            assert(got == p);
            ++received;
        }));
    assert(received == 1);
    Bytes bad = {0x83, 0, 2};
    assert(!f.feed(bad.data(), bad.size(), [](const Bytes &) {}));
    bad = {0x83, 0x10, 1};
    assert(!f.feed(bad.data(), bad.size(), [](const Bytes &) {}));
    std::cout << "PASS OBEX framing and minimum peer MTU for notification events\n";
}
static void fuzz_test() {
    std::mt19937 random(20260920);
    messages::Store box;
    MasServer server(box);
    for (int i = 0; i < 10000; ++i) {
        Bytes bytes(random() % 256);
        for (auto &v : bytes)
            v = random();
        ancs::NotificationResponse parser;
        Notice n;
        parser.feed(bytes.data(), bytes.size(), random(), n);
        dashlink::Decoder wire;
        wire.feed(bytes.data(), bytes.size(), [](dashlink::Packet) {});
        ObexFramer obex;
        obex.feed(bytes.data(), bytes.size(), [](const Bytes &) {});
        if (i % 100 == 0)
            connect(server);
        if (bytes.size() >= 3) {
            bytes[1] = bytes.size() >> 8;
            bytes[2] = bytes.size();
        }
        auto result = server.request(bytes);
        assert(result.size() >= 3 && result.size() <= 4096);
    }
    std::cout << "PASS 10,000 deterministic malformed-input cases (run with sanitizers)\n";
}
static void console_test() {
    runtime::ConsoleCommands parser;
    using C = runtime::Command;
    std::vector<C> commands;
    auto feed = [&](const std::string &bytes) {
        for (unsigned char byte : bytes) {
            auto command = parser.feed(byte);
            if (command != C::none) commands.push_back(command);
        }
    };
    feed("te");
    assert(commands.empty());
    feed("st\r\nPAIR\n help \r\n\r\n");
    assert((commands == std::vector<C>{C::test, C::pair, C::help}));
    commands.clear();
    feed("pair phone\r\npair car\nSTATUS\n");
    assert((commands == std::vector<C>{C::pair_phone, C::pair_car, C::status}));
    commands.clear();
    feed("audio phone tone\naudio phone loopback\naudio car tone\naudio car loopback\naudio off\n");
    assert((commands == std::vector<C>{C::audio_phone_tone, C::audio_phone_loopback, C::audio_car_tone, C::audio_car_loopback, C::audio_off}));
    commands.clear();
    feed("restart\n");
    feed(std::string(32, 'x') + "test\r\n");
    feed(std::string("te\0st\n", 6));
    feed("test\n");
    assert((commands == std::vector<C>{C::invalid, C::invalid, C::invalid, C::test}));
    commands.clear();
    feed(std::string(10000, 'x') + "pair\nhelp\n");
    assert((commands == std::vector<C>{C::invalid, C::help}));
    runtime::SetupLines setup;
    std::vector<std::string> requests;
    for (unsigned char byte : std::string("db allow com.Example.Chat 1\r\ndb status\n")) {
        auto line = setup.feed(byte);
        if (!line.empty()) requests.push_back(line);
    }
    assert((requests == std::vector<std::string>{"db allow com.Example.Chat 1", "db status"}));
    for (unsigned char byte : std::string(200, 'x') + "db deny com.Example.Chat\n")
        assert(setup.feed(byte).empty());
    for (unsigned char byte : std::string("db apps\n")) {
        auto line = setup.feed(byte);
        if (!line.empty()) requests.push_back(line);
    }
    assert(requests.back() == "db apps");
    std::cout << "PASS USB commands: fragmented input, CRLF once, bounds, invalid lines and recovery\n";
}
static void single_board_test() {
    using namespace dashbridge::transport;
    LocalBridge link;
    dashlink::Packet message;
    assert(!link.pop_for_phone(message) && !link.pop_for_car(message));
    // Saturation cannot block a reset or leak old messages into a new session.
    for (size_t i = 0; i < LocalBridge::capacity; ++i)
        assert(link.send_to_car(dashlink::Notification{messages::ChangeKind::add, 1, example(unsigned(i))}));
    assert(!link.send_to_car(dashlink::Notification{messages::ChangeKind::add, 1, example(999)}));
    assert(link.send_to_car(dashlink::Notification{messages::ChangeKind::reset, 2, {}}));
    assert(link.queued() == 1);
    assert(link.send_to_car(dashlink::Notification{messages::ChangeKind::add, 2, example(3)}));
    messages::Store inbox;
    assert(link.pop_for_car(message));
    auto reset = std::get<dashlink::Notification>(message);
    assert(reset.kind == messages::ChangeKind::reset);
    apply(inbox, reset.kind, reset.session, reset.message);
    assert(link.pop_for_car(message));
    auto added = std::get<dashlink::Notification>(message);
    assert(added.message.id == 3);
    assert(apply(inbox, added.kind, added.session, added.message) != 0);
    assert(!link.pop_for_car(message));
    auto oversized = example();
    oversized.title = std::string(200, 'T');
    oversized.body = std::string(767, 'x') + "👋";
    oversized.subtitle = std::string("a\x01\n\tb", 5);
    auto encoded = dashlink::encode(dashlink::Notification{messages::ChangeKind::add, 2, oversized});
    dashlink::Packet wire_result;
    dashlink::Decoder wire;
    wire.feed(encoded.data(), encoded.size(), [&](dashlink::Packet packet) { wire_result = std::move(packet); });
    assert(link.send_to_car(dashlink::Notification{messages::ChangeKind::add, 2, oversized}));
    assert(link.pop_for_car(message));
    const auto local = std::get<dashlink::Notification>(message);
    const auto remote = std::get<dashlink::Notification>(wire_result);
    assert(local.message.title.size() == 128);
    assert(local.message.body == std::string(767, 'x'));
    assert(local.message.subtitle == "a\n\tb");
    assert(local.message.body == remote.message.body && local.message.title == remote.message.title
           && local.message.subtitle == remote.message.subtitle);
    assert(inbox.messages().size() == 1);
    // Readiness updates coalesce in each direction rather than growing queues.
    for (unsigned i = 0; i < 1000; ++i) {
        link.send_to_phone(dashlink::Heartbeat{dashlink::Board::car, 0, i % 2});
        assert(link.send_to_car(dashlink::Heartbeat{dashlink::Board::phone, 2, 0}));
    }
    assert(link.queued() == 1);
    assert(link.pop_for_phone(message) && std::get<dashlink::Heartbeat>(message).flags == 1);
    assert(!link.pop_for_phone(message));
    assert(link.pop_for_car(message) && std::get<dashlink::Heartbeat>(message).session == 2);
    assert(!link.pop_for_car(message));
    std::cout << "PASS single-board routing, bounded queues, session reset and independent pairing\n";
}
static void ancs_flags_test() {
    // Values from Apple's ANCS specification, independent of production constants.
    // Fresh notifications may offer Dismiss (0x10) and Reply (0x08).
    for (uint8_t flags : {0x00, 0x10, 0x18, 0x1b}) {
        messages::Store inbox;
        uint64_t handle = 0;
        if (!ancs::is_preexisting(flags))
            handle = apply(inbox, messages::ChangeKind::add, 1, example());
        assert(handle != 0 && inbox.messages().size() == 1);
    }
    // Old Notification Center entries become silent history: stored, no new-message handle.
    for (uint8_t flags : {0x04, 0x14, 0x1c, 0x1f}) {
        messages::Store inbox;
        auto kind = ancs::is_preexisting(flags) ? messages::ChangeKind::history_add
                                                : messages::ChangeKind::add;
        auto handle = apply(inbox, kind, 1, example());
        assert(!handle && inbox.messages().size() == 1);
    }
    std::cout << "PASS ANCS fresh notifications alert; pre-existing notifications import silently\n";
}
static void heartbeat_origin_test() {
    dashlink::Packet car = dashlink::Heartbeat{dashlink::Board::car, 0, 0};
    dashlink::Packet phone = dashlink::Heartbeat{dashlink::Board::phone, 42, 0};
    assert(dashlink::is_car_heartbeat(car) && !dashlink::is_phone_heartbeat(car));
    assert(dashlink::is_phone_heartbeat(phone) && !dashlink::is_car_heartbeat(phone));
    // Reflected heartbeats cannot refresh the opposite board's link state.
    assert(!dashlink::is_car_heartbeat(phone) && !dashlink::is_phone_heartbeat(car));
    dashlink::Packet reset = dashlink::Notification{messages::ChangeKind::reset, 42, {}};
    assert(!dashlink::is_car_heartbeat(reset) && !dashlink::is_phone_heartbeat(reset));
    std::cout << "PASS board heartbeats reject reflected or wrong-operation frames\n";
}
int main() {
    app_policy_test();
    ancs_flags_test();
    heartbeat_origin_test();
    single_board_test();
    console_test();
    dashlink_test();
    dashlink_types_test();
    ancs_test();
    inbox_test();
    phonebook_test();
    contact_transfer_test();
    map_test();
    framer_test();
    fuzz_test();
}
