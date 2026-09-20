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
