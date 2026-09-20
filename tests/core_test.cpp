#include "bridge_core.hpp"
#include "../firmware/main/console_commands.hpp"
#include "../firmware/main/local_bridge.hpp"
#include <cassert>
#include <iostream>
#include <random>
using namespace bridge;

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
static std::string get_body(MasServer &s, Bytes r, size_t mtu = 255) {
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
static void wire_test() {
    WireMessage input{Op::add, 1234, example()};
    auto packet = encode(input);
    for (size_t cut = 0; cut <= packet.size(); ++cut) {
        WireDecoder decoder;
        int received = 0;
        auto check = [&](const WireMessage &m) {
            ++received;
            assert(m.session == 1234 && m.op == Op::add);
            assert(m.notice.id == 42 && m.notice.body == input.notice.body);
        };
        decoder.feed(packet.data(), cut, check);
        decoder.feed(packet.data() + cut, packet.size() - cut, check);
        assert(received == 1);
    }
    WireDecoder decoder;
    int received = 0;
    auto count = [&](const WireMessage &) { ++received; };
    auto corrupt = packet;
    corrupt[15] ^= 1;
    decoder.feed(corrupt.data(), corrupt.size(), count);
    assert(received == 0);
    decoder.feed(packet.data(), packet.size(), count);
    assert(received == 1);
    Notice long_text = example();
    long_text.body = std::string(767, 'a') + "👋";
    auto clipped = encode({Op::add, 1, long_text});
    decoder.feed(clipped.data(), clipped.size(),
                 [&](const WireMessage &m) { assert(m.notice.body == std::string(767, 'a')); });
    std::cout << "PASS UART fragmentation, checksum recovery, UTF-8 boundary\n";
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
        AncsResponse parser;
        Notice got;
        assert(parser.feed(p.data(), cut, 42, got) == 0);
        assert(parser.feed(p.data() + cut, p.size() - cut, 42, got) == 1);
        assert(got.app == n.app && got.body == n.body && got.date == n.date);
    }
    AncsResponse parser;
    Notice got;
    assert(parser.feed(p.data(), p.size(), 99, got) == -1);
    Bytes too_big(2049, 0);
    assert(parser.feed(too_big.data(), too_big.size(), 42, got) == -1);
    Bytes bad = {0, 42, 0, 0, 0, 0, 0xff, 0xff};
    assert(parser.feed(bad.data(), bad.size(), 42, got) == -1);
    std::cout << "PASS ANCS fragmented attributes, wrong UID and oversize rejection\n";
}
static void inbox_test() {
    Inbox box;
    auto n = example();
    auto h = box.apply({Op::add, 1, n});
    assert(h);
    assert(!box.apply({Op::add, 1, n}) && box.messages().size() == 1);
    n.body = "Edited";
    assert(!box.apply({Op::update, 1, n}));
    assert(box.find(h)->notice.body == "Edited");
    n.id = 99;
    assert(!box.apply({Op::update, 1, n}));
    assert(box.messages().size() == 1);
    n.app = "com.apple.MobileSMS";
    assert(!box.apply({Op::add, 1, n}));
    box.apply({Op::remove, 1, example()});
    assert(box.messages().empty());
    box.apply({Op::add, 2, example()});
    box.apply({Op::reset, 3, {}});
    assert(box.messages().empty());
    for (int i = 0; i < 40; ++i)
        box.apply({Op::add, 3, example(i)});
    assert(box.messages().size() == 32);
    std::cout << "PASS WhatsApp filtering, duplicate/update handling, session clearing and bounds\n";
}
static void map_test() {
    Inbox box;
    auto h = box.apply({Op::add, 1, example()});
    MasServer server(box);
    assert(server.request(obex_packet(0x83))[0] == 0xc1);
    connect(server);
    folder(server, "telecom");
    folder(server, "msg");
    auto listing =
        get_body(server, server.request(obex_packet(0x83, headers("x-bt/MAP-msg-listing", "inbox"))));
    assert(listing.find("Alice &amp; Bob") != std::string::npos);
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
    Inbox box;
    MasServer server(box);
    for (int i = 0; i < 10000; ++i) {
        Bytes bytes(random() % 256);
        for (auto &v : bytes)
            v = random();
        AncsResponse ancs;
        Notice n;
        ancs.feed(bytes.data(), bytes.size(), random(), n);
        WireDecoder wire;
        wire.feed(bytes.data(), bytes.size(), [](const WireMessage &) {});
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
    feed("restart\n");
    feed(std::string(32, 'x') + "test\r\n");
    feed(std::string("te\0st\n", 6));
    feed("test\n");
    assert((commands == std::vector<C>{C::invalid, C::invalid, C::invalid, C::test}));
    commands.clear();
    feed(std::string(10000, 'x') + "pair\nhelp\n");
    assert((commands == std::vector<C>{C::invalid, C::help}));
    std::cout << "PASS USB commands: fragmented input, CRLF once, bounds, invalid lines and recovery\n";
}
static void single_board_test() {
    using namespace runtime;
    PairingWindows pairing;
    for (auto first : {Peer::phone, Peer::car}) {
        auto second = first == Peer::phone ? Peer::car : Peer::phone;
        pairing.open(first, 1000);
        pairing.open(second, 1000);
        pairing.paired(first);
        assert(!pairing.allowed(first, 1001));
        assert(pairing.allowed(second, 1001));
        assert(pairing.allowed(second, 120999));
        assert(!pairing.allowed(second, 121000));
    }
    LocalBridge link;
    WireMessage message{};
    assert(!link.pop_for_phone(message) && !link.pop_for_car(message));
    // Saturation cannot block a reset or leak old messages into a new session.
    for (size_t i = 0; i < LocalBridge::capacity; ++i)
        assert(link.send_to_car({Op::add, 1, example(unsigned(i))}));
    assert(!link.send_to_car({Op::add, 1, example(999)}));
    assert(link.send_to_car({Op::reset, 2, {}}));
    assert(link.queued() == 1);
    assert(link.send_to_car({Op::add, 2, example(3)}));
    Inbox inbox;
    assert(link.pop_for_car(message) && message.op == Op::reset);
    inbox.apply(message);
    assert(link.pop_for_car(message) && message.notice.id == 3);
    assert(inbox.apply(message) != 0);
    assert(!link.pop_for_car(message));
    assert(inbox.messages().size() == 1);
    // Readiness updates coalesce in each direction rather than growing queues.
    for (unsigned i = 0; i < 1000; ++i) {
        Notice heartbeat;
        heartbeat.id = i % 2;
        link.send_to_phone({Op::heartbeat, 0, heartbeat});
        assert(link.send_to_car({Op::heartbeat, 2, {}}));
    }
    assert(link.queued() == 1);
    assert(link.pop_for_phone(message) && message.notice.id == 1);
    assert(!link.pop_for_phone(message));
    assert(link.pop_for_car(message) && message.session == 2);
    assert(!link.pop_for_car(message));
    std::cout << "PASS single-board routing, bounded queues, session reset and independent pairing\n";
}
int main() {
    single_board_test();
    console_test();
    wire_test();
    ancs_test();
    inbox_test();
    map_test();
    framer_test();
    fuzz_test();
}
