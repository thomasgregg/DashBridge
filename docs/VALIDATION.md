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

## Hardware tests: NOT RUN

No ESP32, iPhone or Tesla was connected to this development session. The following remain unverified:

| Test | Result |
|---|---|
| Both boards boot and stay within available RAM | Not run |
| Tesla discovers B and enables message sync | Not run |
| B's standalone test message appears on Tesla | Not run |
| iPhone pairs with A and grants ANCS access | Not run |
| New WhatsApp content reaches Tesla end to end | Not run |
| Locked iPhone and closed setup app | Not run |
| Power loss, repeated reconnects and no old-message replay | Not run |
| Group messages, emojis, previews, rapid bursts | Not run |
| Phone key remains functional with both boards connected | Not run |
| Long-duration stability | Not run |

Calls/audio passthrough and outgoing WhatsApp replies are not implemented, so they are not awaiting a passing test in this version. This prototype is not yet a reliable everyday replacement for the iPhone's normal Tesla Bluetooth connection.
