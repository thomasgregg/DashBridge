<p align="center"><img src="web/favicon.svg" width="64" alt="DashBridge" /></p>

# DashBridge

**Your iPhone notifications, on your Tesla dashboard.**

DashBridge receives iPhone notifications over Bluetooth, filters WhatsApp and
presents them through the Tesla's message interface. No SMS forwarding, server
or WhatsApp account login is involved.

> **Two-board call relay — development branch.** Notifications have worked on the
> earlier single-board prototype. This branch adds a headset/phone proxy so calls
> can pass through DashBridge too. The new call relay is not hardware-validated.
> Music and contact syncing are not implemented yet.

[Wiring and first call test](docs/CALL_RELAY.md) · [Working notification prototype](https://github.com/thomasgregg/DashBridge/tree/main) · [Original two-board release](https://github.com/thomasgregg/DashBridge/releases/tag/v0.1.4) · [MIT license](LICENSE)

## Why two boards again?

The single-board prototype delivered a real WhatsApp notification to the Tesla,
but occupied its active phone connection. A message-only experiment also displaced
the iPhone. The new approach makes DashBridge the active phone and relays iPhone
calls through it.

```text
iPhone ← Bluetooth → Board A ← wired data + audio → Board B ← Bluetooth → Tesla
```

Board A is the iPhone's headset and notification receiver. Board B presents the
phone and messages to the Tesla. The pinned ESP32 Bluetooth stack cannot run its
hands-free and audio-gateway roles simultaneously, so each board handles one role.

## First milestone

The source includes call state, answer/reject/end controls, explicit-number dialing, DTMF, narrowband audio
in both directions and reconnection attempts. It preserves the working ANCS and
Tesla message-service fixes. The [call-relay guide](docs/CALL_RELAY.md) explains the
five wires, pairing and the parked-car tests.

This is a **one-call prototype**. Start the first outgoing test call on the iPhone.
Music, media controls, contacts, Siri, redial and multiparty calls remain work
for later milestones. A successful build is not evidence of working call audio.

The [public installer](https://thomasgregg.github.io/DashBridge/) still serves the
single-board notification prototype. The relay is developed separately, and
[v0.1.4](https://github.com/thomasgregg/DashBridge/releases/tag/v0.1.4) remains an
unchanged rollback point for the original two-board design.

## Develop

With ESP-IDF v5.5.1 activated:

```sh
bash tools/test.sh
python3 tools/test_build_scope.py
bash tools/build.sh both
```

`bash tools/build.sh` rebuilds only stale A/B images. Use `phone` or `car` to build
one explicitly. Documentation and installer changes do not require firmware builds.

## License

DashBridge is MIT licensed. ESP-IDF and the ESP32 controller have their own
notices; see [third-party notices](third_party/README.md).
