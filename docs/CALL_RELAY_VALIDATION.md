# Call relay validation — 20 September 2026

Version: **0.3.0-call-alpha**. Firmware source commit: `dcfb6a5`.

[Final CI run, both targets](https://github.com/thomasgregg/DashBridge/actions/runs/35525366223)

| Check | Result |
| --- | --- |
| Existing ANCS, notification transport, MAP and console host tests | PASS, AddressSanitizer + UndefinedBehaviorSanitizer |
| Call state parsing, malformed numbers, allowed commands, duplicate rejection | PASS |
| Audio framing, CRC rejection, resynchronization and bounded PCM buffer | PASS |
| Audio sequence wrap, duplicates and recovery after a long interruption | PASS |
| Build-scope tests, including only stale A/B selection | PASS, 6 tests |
| ESP32 Board A compile/link | PASS, no compiler warnings |
| ESP32 Board B compile/link | PASS, no compiler warnings |
| Generated HFP role, internal codec, CVSD-only audio and task tick | PASS on both boards |
| Gateway compiler flags exclude unsupported Siri/in-band/ECNR features | PASS |
| Production MAP SDP record and captured Tesla SDP query replays | PASS |
| Downloaded image sizes, hashes, ESP32 header and current source hashes | PASS |
| Two physical boards, iPhone and Tesla call/audio test | **NOT RUN — second board has not arrived** |

Application size: A `0xfdd60` bytes, B `0xfff80` bytes; both leave **32%** of the
application partition free. The merged flash-image sizes and SHA-256 checksums
are recorded in [`dist/manifest.json`](../dist/manifest.json). Both images are
explicitly marked `hardware_tested: false`.

No board was flashed during this work. Bluetooth interoperability, caller display,
answer/reject/dial behavior, microphone/speaker audio, echo, audio-route recovery,
reconnection and sustained operation remain hardware tests. The host tests do
not simulate the complete iPhone/Tesla Bluetooth state machines.

Use the [wiring and test guide](CALL_RELAY.md) when both boards are available.
Music, media controls, contacts, Siri, redial and multiparty calling are outside
this first milestone. At the time of this firmware build, the public installer was unchanged. The
installer was subsequently updated to offer these A/B alpha images; the
v0.1.4 rollback release remains unchanged.

## Board B reconnection investigation

The user subsequently tested Board B alone on the Tesla with `0.3.0-call-alpha`:

- Pairing, message syncing and a test notification were reported working.
- One reset was followed by the phone profile becoming ready after about 26 seconds;
  message notifications were ready immediately afterwards.
- Reconnection after USB power removal was not reliable. Failures also occurred
  with B set as priority and the iPhone manually disconnected in Tesla settings.
- Failure logs show Bluetooth links opening, closing with reason `0x13`, and the
  phone profile remaining disconnected. That reason denotes termination by the
  remote device; it does not explain the Tesla's decision.

Firmware source `4d53d36` adds reconnect diagnostics without changing the retry
policy: saved-peer/bond checks, outgoing request results, retry timing, HFP states
and Bluetooth link events. Its startup marker is `Reconnect diagnostics 1`.
The second board and end-to-end call/audio tests are still outstanding. These
observations do not establish reliable automatic reconnection.

The installer log-download action was also corrected: downloading logs no longer
resets the board. After unplugging USB, refresh the installer and reopen
**Logs & Console** to start a new serial session; earlier startup output may be
missing. Capture both a failed automatic attempt and a manual Tesla connection
in the same session, without resetting between them.

The [diagnostic firmware CI run](https://github.com/thomasgregg/DashBridge/actions/runs/35530031334)
passed both ESP32 builds, host protocol tests, call configuration checks and the
Tesla SDP tests. Packaged images were checked against their source and binary
checksums before publication. This diagnostic build has not yet been tested on
the physical board.


## Tesla-initiated reconnect experiment

The subsequent diagnostic log shows outgoing attempts 5 and 6 failing, while a
manual Tesla connection at 20:56:08 reaches HFP ready in under a second. MAP is
ready at 20:56:09; at 20:56:14 the Tesla retrieves the test message and acknowledges
the event. The saved bond remains valid throughout. The user also confirms that
the iPhone reconnects while they remain seated: leaving and returning is not an
acceptable substitute for the same stationary power-cycle test.

Board B now leaves its incoming HFP service available instead of initiating HFP
connections. The pinned SDK closes its incoming HFP servers during an outgoing
open (`bta_ag_start_open`); interference with the Tesla's own attempts is a
hypothesis, not an established root cause. A retains its iPhone reconnect policy.
Message notification connections and call audio setup remain enabled on B.

The startup marker is `Tesla reconnect test 1: incoming-only`. `status` reports
`Tesla reconnect: incoming-only` instead of an outgoing retry timer. Hardware
validation of this experiment is pending; automatic reconnection is not yet fixed.

After installing B and re-pairing once, verify a test notification, keep B as the
priority device, and disconnect the iPhone's Tesla phone connection (leave Phone
Key untouched). While still parked and seated, remove B's USB power for five
seconds, restore it, and wait 90 seconds without manually connecting. Reopen the
USB console and capture `status`. A successful result requires an automatic
connection and another delivered test message. If it fails, capture that log
before manually connecting, then capture the manual connection in the same log.

Source `fcdbc5b` passed [firmware CI](https://github.com/thomasgregg/DashBridge/actions/runs/35531040216):
both ESP32 builds, protocol and reconnect-policy tests, call configuration checks,
and Tesla SDP checks. The reconnect test executes the production polling block
for both roles: A retains retries and B makes no outgoing or cancellation calls.
Packaged binaries match the current source hashes and their recorded SHA-256
checksums. These software checks do not establish successful Tesla reconnection;
the stationary power-cycle hardware test remains pending.

### Result and outgoing discovery trace

The subsequent captured reset at 21:14:14 retained `stored=1 bonded=1`. No
incoming ACL connection was logged in the next 90 seconds. At 21:16:10 a
connection consistent with the requested manual Connect action brought HFP and
MAP online within a second. The incoming-only experiment did not meet the
automatic reconnect requirement. An earlier Device ID query/disconnect does
not establish the cause.

The next diagnostic build restores the previous bounded outgoing HFP retries
and adds Board B traces inside the pinned SDK: discovery UUID and result,
SDP client requests/responses, SDP server responses, selected RFCOMM channel,
RFCOMM events and the gateway open status. Packet dumps are capped at 192 bytes
and contain only service-discovery data, never message or audio payloads.
No advertised services or pairing policy are changed. Board A keeps its
existing retry behaviour. The startup marker is
`Tesla reconnect test 2: outgoing HFP with SDP trace`.

After flashing B and pairing once, keep the serial console open, reset B, and
wait 90 seconds with B selected as Tesla's priority device and the iPhone's
Tesla phone connection disconnected. If automatic reconnect fails, manually
connect once and download the same log. This is evidence collection, not a
verified reconnection fix.

Source `784b004` passed [CI run 35532043580](https://github.com/thomasgregg/DashBridge/actions/runs/35532043580):
both ESP32 builds, role/configuration checks, host protocol and reconnect tests,
the captured Tesla SDP query tests, and the bounded packet-trace tests. The
build log contains no compiler warnings/errors. The packaged B image contains
all new diagnostic markers; A contains no added SDK packet tracing. Both images
match their source hashes and binary checksums, and all 11 installer tests pass.
The diagnostic firmware has not yet been tested on the physical board.

### Pairing lost after a failed controller feature query

The diagnostic build subsequently connected HFP and MAP at 21:31:49. After a
reset at 21:32:05, its first outgoing reconnect encountered remote-feature
query failures with HCI status `0x02` (unknown connection identifier). At
21:32:13 the SDK logged `Remote Device downgraded security from SC, deleting Link Key`;
the bond changed from present to absent. The user's subsequent manual Connect
attempt at 21:33:40 failed with `bonded=0`.

In the pinned SDK, `btm_read_remote_ext_features_failed` still processes partial
feature pages and continues establishment after `HCI_ERR_NO_CONNECTION`.
Processing those pages can feed missing Secure Connections feature bits into
the existing downgrade detector. The Board B patch returns on that specific
failed-query status, without processing capabilities or continuing connection
establishment on the invalid handle. It does not modify successful feature
handling, the downgrade detector, authentication or encryption requirements.
An executable regression test compares the original and patched callback,
including repeated late events, missing handles and another error status.

This targets unintended bond loss and the resulting manual-connect failure;
it does not establish the cause of Tesla terminating the original outgoing
connection or guarantee automatic reconnection. The already-deleted pairing
will need to be recreated after installing the corrected B image.

The corrected B image is `0.3.1-alpha+3421de5e2b6e`, built and tested in
[CI run 35532949574](https://github.com/thomasgregg/DashBridge/actions/runs/35532949574).
Its source hashes, embedded version and packaged checksum were verified, and
the compiler log contains no warnings/errors. A's versioned image
`0.3.1-alpha+c7b2850b78fe` is reused from
[CI run 35532642060](https://github.com/thomasgregg/DashBridge/actions/runs/35532642060);
the ACL correction changes only B. All 13 installer tests pass, including
selection-specific version display and rejection of incorrectly labelled
binaries. The corrected B image still requires a physical reconnect test.
