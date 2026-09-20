# DashBridge validation record — 20 September 2026

## Host software tests: PASS

Command: `bash tools/test.sh`, using Apple Clang, C++17, warnings as errors, AddressSanitizer and UndefinedBehaviorSanitizer. Output is recorded in `host-test-results.txt`.

- UART frames split at every byte boundary; CRC-corrupted frame rejected and next frame recovered.
- UTF-8 truncation does not leave a partial emoji at the body limit.
- ANCS attribute response split at every byte boundary; incorrect UID and excessive lengths rejected.
- App filtering, duplicate add suppression, updates/removals, new-session clearing and 32-entry bound.
- MAP connection requirement, listing/XML escaping, pagination, 255-byte peer-MTU continuations and UTF-8 bMessage byte length.
- Unsupported native SMS encoding and outgoing-message requests rejected.
- Notification registration and local read status.
- Fragmented OBEX packets; invalid packet lengths rejected.
- MNS events fit the minimum supported peer MTU; optional Connection-ID and undersized peer MTU handling.
- 10,000 seeded malformed-input cases through the UART, ANCS and OBEX parsers and MAP request handler, with no sanitizer findings.

The Python packaging/flashing helpers pass syntax checks. The flash helper's help path was exercised without opening a hardware port.

## ESP32 firmware compilation: PASS

Both roles compiled and linked successfully with ESP-IDF v5.5.1 for the original ESP32. Neither build produced compiler warnings or errors. The reproducible build helper was then exercised for each role and used to create the merged flash images.

| Image | Application bytes | Application partition free |
|---|---|---|
| Board A / iPhone | `0x103200` | 31% |
| Board B / Tesla | `0x1052d0` | 30% |

These are application sizes, excluding bootloader and merged-image padding. `dist/manifest.json` records merged-image sizes, checksums and source-file checksums. Flashing was not attempted because the boards were not connected.

## First hardware report and startup fix

The first user-provided USB logs confirm that A reaches its startup message.
After installing B, the board repeatedly aborts in `runtime::record()` with
`Invalid server name!` and `ESP_ERR_INVALID_ARG` from `esp_sdp_create_record`.
The submitted SDP service-name length excluded the terminating NUL, which
ESP-IDF v5.5.1 explicitly requires. Version 0.1.1 includes that byte.

A host check exercised the actual `record()` construction against the pinned
SDK's `esp_sdp_record_integrity_check`: the corrected record passes, and reducing
its name length by one reproduces the validation failure. Protocol sanitizer
tests also pass. This verifies the reported argument error, not Bluetooth radio
operation; the corrected image still needs a hardware retest.

The v0.1.1 hardware retest reaches `MAP service record status=0`, connects to
the Tesla's phone profile, and no longer shows the original abort loop. Enabling
Sync Messages does not persist: the board repeatedly returns SDP error 0x03
before any MAP transport connection is logged. The exact rejected request is
not present in that log. Version 0.1.2 adds bounded SDP request bytes and explicit
rejection reasons for diagnosis; it does not claim to fix message sync.

The v0.1.2 hardware log identifies `BAD_ATTR_LIST`: Tesla requests 8 attributes
for MAP and 10 for Device ID; the phone-profile request has 7 and succeeds.
Replaying those packets through the pinned SDK reproduces the rejection: its
8-entry array is checked after incrementing the count, rejecting even exactly
8 entries. Version 0.1.3 uses a 16-entry capacity and checks before writing.
Host replay tests accept all three captured requests, accept exactly 16 entries,
reject 17 and 32 without sanitizer findings, and preserve range requests.
Tesla message sync and message delivery still require a physical retest.

The pictured board has only an RST button. BOOT-based pairing and test-message
controls cannot be used on that board without an alternative input. Before any
pairing is saved, restarting opens the two-minute pairing window automatically.

## Hardware compatibility: UNVERIFIED

The user is testing an ESP32 with a Tesla; no hardware is connected to the build/test process. The following remain unverified:

| Test | Result |
|---|---|
| Both boards boot and stay within available RAM | A and B startup observed; sustained RAM/stability testing pending |
| Tesla discovers B and enables message sync | B discovered and phone profile connects; Sync Messages reverts off |
| B's standalone test message appears on Tesla | Not run |
| iPhone pairs with A and grants ANCS access | Not run |
| New WhatsApp content reaches Tesla end to end | Not run |
| Locked iPhone and closed setup app | Not run |
| Power loss, repeated reconnects and no old-message replay | Not run |
| Group messages, emojis, previews, rapid bursts | Not run |
| Phone key remains functional with both boards connected | Not run |
| Long-duration stability | Not run |

Calls/audio passthrough and outgoing WhatsApp replies are not implemented, so they are not awaiting a passing test in this version. This prototype is not yet a reliable everyday replacement for the iPhone's normal Tesla Bluetooth connection.
