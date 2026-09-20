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
this first milestone. The public notification installer and the v0.1.4 rollback
release were not replaced by this experimental build.
