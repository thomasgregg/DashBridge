# DashBridge implementation notes

For the current two-board call relay, start with [CALL_RELAY.md](CALL_RELAY.md).
The notes below describe the earlier notification-only implementation, before
call and audio relaying were added. The single-board prototype is preserved at
[archive/single-board](https://github.com/thomasgregg/DashBridge/tree/archive/single-board),
with its historical notes in [SINGLE_BOARD.md](SINGLE_BOARD.md).

## Transport and state

Board A is a BLE peripheral and GATT client of the iPhone's ANCS service. It solicits ANCS in its advertisement, requests bonding and encryption, discovers the three ANCS characteristics, subscribes to Data Source before Notification Source, and serializes attribute requests. Each request asks for application identifier, title, subtitle, body and date. It filters for `net.whatsapp.WhatsApp` and `net.whatsapp.WhatsAppSMB` after receiving attributes. Because Notification Source does not contain an app identifier, A necessarily receives attributes from other apps too, but does not forward them.

Pre-existing events are skipped. An ANCS removal cancels pending requests for that UID. A timeout or malformed response disconnects to reset stream synchronization. Service-change events invalidate the GATT cache. These paths are implemented but BLE lifecycle behavior needs device testing.

Board B is a Classic Bluetooth MAP Message Access Server with a minimal HFP Audio Gateway. Espressif's public SDP API advertises the MAP record; SPP callback mode supplies RFCOMM on channel 4. This also creates a separate SPP service record. The car must accept that service arrangement. A second RFCOMM connection connects to the car's Message Notification Server discovered through SDP. Both directions require link authentication and encryption.

There is **no PBAP contacts service, audio routing, A2DP or HFP call passthrough**. HFP reports no cellular service and no calls; outgoing calls and unsupported message PUT operations fail. The Tesla may require additional profile behavior before enabling messages. That is why the Board B test precedes iPhone setup.

## MAP subset

- OBEX connect, disconnect, abort, set-path and bounded packet framing.
- Folder listing for `telecom/msg/{inbox,outbox,sent,deleted}`.
- Message listing, pagination, count-only requests and basic message-type/read filters.
- UTF-8 SMS_GSM bMessage retrieval and MTU-aware response chunks.
- Notification registration and NewMessage events over MNS.
- Read/delete status affects only the local adapter inbox, never WhatsApp.
- Native GSM PDU body requests and outgoing messages are rejected.

This is not a claim of complete MAP conformance. Unsupported listing filters and parameter masks are not fully implemented; some clients may require them. The message body's group subtitle is preserved. Sender numbers are not available from ANCS. A fake dialable number is not invented.

## Local wire format

UART2, 115200 baud, 8N1, GPIO17 transmit/GPIO16 receive. Frame:

```text
54 57 | version=01 | operation | payload-length BE16 | payload | CRC32 LE32
```

CRC32/IEEE covers version through the end of payload. Payload is session LE32, notification UID LE32, followed by five fields: application, title, subtitle, body, date. Each field has a BE16 byte length and UTF-8 bytes. Sender limits are respectively 128, 128, 128, 768 and 32 bytes. Maximum payload is 1,302 bytes. Operations: reset=1, add=2, update=3, remove=4, heartbeat=5.

Both boards send a heartbeat each second. Board B's heartbeat UID is 1 only when its MAP session, notification registration and MNS connection are ready. A discards offline events and starts a new random session when readiness changes. B clears entries on reset, Tesla disconnect or five seconds without an A frame. A considers B lost after three seconds. There is no offline replay, persistent outbox or delivery guarantee. UART CRC detects corruption but does not acknowledge or retry packets.

The inbox holds at most 32 notifications, identified within a phone session by iOS UID. Repeated adds and edits of retained UIDs do not generate repeated NewMessage events. Handles increase for the lifetime of B. Eviction, iOS grouping and reconnects mean this is not a general exactly-once messaging system. B sends an event once and logs rejection; it does not blindly retry an event that might already have alerted the driver.

Bluetooth callbacks and the main UART loop share a recursive mutex. UART writes are queued outside the lock. SPP writes retain data until their completion callback and honor congestion. Queue bounds intentionally drop excess work rather than grow RAM without limit.

## Build

Use the pinned ESP-IDF release in the README and its supported compiler. The helper checks the IDF version and creates separate configurations/build directories for the two roles. Activate IDF's environment before running it:

```sh
. /path/to/esp-idf/export.sh
bash tools/build.sh
```

The default builds only images whose relevant source checksums have changed.
Pass `phone` (A) or `car` (B) to force one board, or `both` to force both.
Phone implementation/configuration changes affect A; car implementation,
configuration and SDP patches affect B; shared firmware changes affect both.
New or deleted source files and missing/corrupt binaries also mark an image stale.
Build/packaging tool changes also mark images stale. Force a build after changing
the SDK environment; only the pinned SDK is supported.

### Firmware versions

`firmware/version.txt` is the single release-version source. Each board appends
a deterministic 12-character build ID derived from its relevant source and
build-tool checksums, for example `0.3.1-alpha+0123456789ab`. A B-only source edit
changes B's ID without rebuilding or relabelling A. Documentation and web-only
edits change neither ID. Identical source inputs retain the same ID.

CMake embeds that exact identifier into ESP-IDF's application descriptor.
Packaging checks the actual binary's version before writing its manifest; the
website build checks it again. The selected board's complete identifier appears
on the page, in the install dialog, after installation, and in the board's logs.
The page must never strip the prerelease suffix or substitute a global version
for the selected board's version. Change the release-version file for a new
release; ordinary development builds receive new IDs automatically. The complete
identifier must fit ESP-IDF's 31-byte version field.

### Publishing builds

CI uses the same selection and skips compilation when the checked-in images
already match their sources. README and website changes do not trigger firmware
CI. The installer deploys when its web files or packaged firmware change, not
when firmware source alone changes.

Each CI board job produces `dashbridge-firmware-phone` or
`dashbridge-firmware-car`. When publishing an artifact, copy its binary and only
that board's `images` entry from its manifest; keep the other board's entry.
Do not overwrite one board's new entry with the other job's older manifest.

 `tools/package.py` uses IDF's generated flash layout to merge images and records SHA-256 checksums. Source checksums make it possible to detect whether a packaged image predates a source edit.

`tools/test.sh` runs the platform-independent core with AddressSanitizer and UndefinedBehaviorSanitizer using Clang. Host tests do not exercise the ESP32 Bluetooth stack, concurrency or iPhone/Tesla behavior. Record physical results separately rather than promoting a build pass into a compatibility claim.

After building, `python tools/test_sdp.py` checks the production MAP service
record against the activated SDK's actual SDP argument validator. It also checks
that excluding the service name's NUL terminator reproduces the original startup
failure. A host C++ compiler is required; no board is accessed.

## Service-discovery diagnostics

Board B's build uses `firmware/sdp_diagnostics/CMakeLists.txt` to instrument a
build-local copy of ESP-IDF's SDP server. The SDK source remains untouched.
A source checksum makes SDK changes fail configuration until reviewed. Version
0.1.2 logs the first 96 bytes of each incoming SDP request and labels invalid
request branches. These are service-discovery requests, not WhatsApp contents.
Version 0.1.3 also applies a pinned compatibility patch through `patch.py`: the
attribute-list capacity increases from 8 to 16 and the capacity check runs before
writing each entry. The original post-increment check rejected a valid list of
exactly 8 attributes. All Bluetooth source files use the same patched internal
header so the structure layout stays consistent. The SDK installation remains
unchanged, and every patched upstream file is checksum-guarded.

`python tools/test_sdp_attributes.py` replays the captured Tesla HFP (7 fields),
MAP (8 fields), and Device ID (10 fields) requests through both the original and
patched parsers. It reproduces the two original rejections and verifies the
fixed parser, exact capacity, oversized-list rejection and attribute ranges
under address and undefined-behavior sanitizers. Run with `IDF_PATH` set.
