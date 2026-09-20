/* Connection-control diagnostics only. No HCI key material, names, RFCOMM
 * payloads, audio, or arbitrary L2CAP echo data are recorded. Numeric values
 * below are Bluetooth Core wire identifiers, independent of SDK enums. */
#ifndef DASHBRIDGE_CONNECTION_TRACE_H
#define DASHBRIDGE_CONNECTION_TRACE_H
#include <stdint.h>
#include <stddef.h>
#ifndef DB_TRACE
#include "esp_log.h"
#define DB_TRACE(...) ESP_LOGI("dbtrace", __VA_ARGS__)
#endif
static inline unsigned db_u16(const uint8_t *p) { return p[0] | ((unsigned)p[1] << 8); }
static inline void db_hex(const uint8_t *p, size_t n, char *out) {
    const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i) { out[2*i] = hex[p[i] >> 4]; out[2*i+1] = hex[p[i] & 15]; }
    out[2*n] = 0;
}
/* Positive allowlist. Unlisted commands get metadata only, never parameters. */
static inline unsigned db_command_prefix(unsigned op) {
    switch (op) {
    case 0x0405: return 13; /* Create connection: address, packet/scan/clock/role */
    case 0x0406: return 3;  /* Disconnect: handle, reason */
    case 0x0408: return 6;  /* Cancel create connection */
    case 0x0409: case 0x040a: return 7; /* Accept/reject: address, role/reason */
    case 0x0411: return 2; /* Authenticate: handle */
    case 0x0413: return 3; /* Encryption: handle, enable */
    case 0x0419: return 10; /* Remote name request (no name) */
    case 0x041b: case 0x041d: return 2; /* Remote features/version */
    case 0x041c: return 3; /* Extended features: handle, page */
    case 0x042b: return 9; /* IO capabilities, no PIN */
    case 0x042c: case 0x042d: return 6; /* Confirmation accept/reject */
    case 0x0803: return 10; /* Sniff mode parameters */
    case 0x0804: case 0x0809: return 2;
    case 0x080b: return 7; /* Switch role */
    case 0x080d: return 4; /* Link policy */
    case 0x080f: return 2; /* Default link policy */
    case 0x0c1a: return 1; /* Scan enable */
    case 0x0c18: return 2; /* Page timeout */
    case 0x0c24: return 3; /* Class of device */
    case 0x0c1c: case 0x0c1e: case 0x0c37: return 4;
    case 0x1408: return 2; /* Read encryption key SIZE, never key */
    default: return 0;
    }
}
static inline int db_classic_command(unsigned op) {
    unsigned group = op >> 10;
    return group >= 1 && group <= 5;
}
static inline void db_hci_command(const uint8_t *p, size_t n, int status) {
    if (n < 3) return;
    unsigned op = db_u16(p), declared = p[2];
    if (!db_classic_command(op)) return;
    size_t take = db_command_prefix(op);
    if (take > declared) take = declared;
    if (take > n-3) take = n-3;
    char hex[27]; db_hex(p+3, take, hex);
    DB_TRACE("HCI %s op=0x%04x status=%d bytes=%u captured=%u data=%s",
             status < 0 ? "TX" : "STATUS", op, status, declared, (unsigned)take, hex);
}
static inline void db_hci_event(const uint8_t *p, size_t n) {
    if (n < 2) return;
    unsigned event = p[0], declared = p[1];
    size_t available = n-2 < declared ? n-2 : declared, take = 0;
    const uint8_t *body = p+2;
    const char *name;
    switch (event) {
    case 0x03: name="connection-complete"; take=11; break;
    case 0x04: name="connection-request"; take=10; break;
    case 0x05: name="disconnect-complete"; take=4; break;
    case 0x06: name="authentication-complete"; take=3; break;
    case 0x07: name="remote-name-complete"; take=7; break; /* omit actual name */
    case 0x08: name="encryption-change"; take=4; break;
    case 0x0b: name="remote-features"; take=11; break;
    case 0x0c: name="remote-version"; take=8; break;
    case 0x0e:
        if (available < 4 || !db_classic_command(db_u16(body+1))) return;
        name="command-complete"; take=4;
        if (db_u16(body+1) == 0x1408) take=7; /* encryption key size */
        break;
    case 0x0f:
        if (available < 4 || !db_classic_command(db_u16(body+2))) return;
        name="command-status"; take=4; break;
    case 0x10: name="hardware-error"; take=1; break;
    case 0x12: name="role-change"; take=8; break;
    case 0x14: name="mode-change"; take=6; break;
    case 0x16: name="pin-request"; take=6; break;
    case 0x17: name="link-key-request"; take=6; break;
    case 0x18: name="link-key-notification-redacted"; take=6; break;
    case 0x1a: name="buffer-overflow"; take=1; break;
    case 0x1b: name="max-slots"; take=3; break;
    case 0x1c: name="clock-offset"; take=5; break;
    case 0x1d: name="packet-type-change"; take=5; break;
    case 0x23: name="remote-extended-features"; take=13; break;
    case 0x2c: name="synchronous-connection"; take=17; break;
    case 0x2d: name="synchronous-connection-change"; take=9; break;
    case 0x2e: name="sniff-subrating"; take=11; break;
    case 0x30: name="encryption-key-refresh"; take=3; break;
    case 0x31: name="io-capability-request"; take=6; break;
    case 0x32: name="io-capability-response"; take=9; break;
    case 0x33: name="user-confirmation-request"; take=6; break; /* omit number */
    case 0x34: name="passkey-request"; take=6; break;
    case 0x35: name="oob-request"; take=6; break;
    case 0x36: name="simple-pairing-complete"; take=7; break;
    case 0x38: name="supervision-timeout"; take=4; break;
    case 0x3b: name="passkey-notification-redacted"; take=6; break;
    case 0x3d: name="remote-host-features"; take=14; break;
    default: return; /* No vendor/LE/key-return/data events. */
    }
    if (take > available) take = available;
    char hex[35]; db_hex(body, take, hex);
    DB_TRACE("HCI RX event=0x%02x %s bytes=%u captured=%u data=%s",
             event, name, declared, (unsigned)take, hex);
}
/* Whole HCI ACL frame, before headers are consumed. CID 1 is Classic control;
 * payload channels (including security CID 7) and continuations are excluded. */
static inline void db_l2cap_signal(const char *direction, const uint8_t *p, size_t n) {
    if (n < 8 || ((db_u16(p) >> 12) & 3) == 1 || db_u16(p+6) != 1) return;
    unsigned handle = db_u16(p) & 0xfff, hci_len = db_u16(p+2), l2_len = db_u16(p+4);
    if (hci_len < 4 || hci_len > n-4 || l2_len > hci_len-4) {
        DB_TRACE("L2CAP %s handle=0x%x malformed lengths", direction, handle); return;
    }
    p += 8; n = l2_len;
    while (n >= 4) {
        unsigned code=p[0], id=p[1], length=db_u16(p+2);
        if (length > n-4) { DB_TRACE("L2CAP %s handle=0x%x truncated code=%u", direction, handle, code); return; }
        size_t take = 0;
        switch (code) {
        case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 10: case 11:
            take = length < 48 ? length : 48; break;
        default: break; /* Echo and unrecognized payloads omitted. */
        }
        char hex[97]; db_hex(p+4, take, hex);
        DB_TRACE("L2CAP %s handle=0x%x code=0x%02x id=%u bytes=%u captured=%u data=%s",
                 direction, handle, code, id, length, (unsigned)take, hex);
        p += 4+length; n -= 4+length;
    }
}
#endif
