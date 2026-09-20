<p align="center"><img src="web/favicon.svg" width="64" alt="DashBridge" /></p>

# DashBridge

**Your iPhone notifications, on your Tesla dashboard. One ESP32.**

This branch develops a single-board Bluetooth bridge. It receives iPhone
notifications over Bluetooth Low Energy, filters WhatsApp notifications,
and presents them through the Tesla's Bluetooth message interface.
No SMS forwarding, WhatsApp account login, server, or second board is needed
by this design.

> **Experimental single-board prototype.** Both Bluetooth connections and a
> Tesla test notification have worked on hardware. Real WhatsApp delivery is
> still being debugged. Calls, music passthrough and replies are not implemented.
> The Tesla may use DashBridge as its active phone, affecting the iPhone's normal
> call connection.

[Setup and testing](docs/SINGLE_BOARD.md) · [Two-board release](https://github.com/thomasgregg/DashBridge/releases/tag/v0.1.4) · [MIT license](LICENSE)

## How it works

```text
iPhone ── Bluetooth LE ── ESP32 ── Bluetooth Classic ── Tesla
                         DashBridge
```

One original ESP32-WROOM-32 with 4 MB flash runs both Bluetooth services.
Notifications pass through an internal queue; no jumper wires are needed.
The board filters WhatsApp and WhatsApp Business notifications and preserves
normal iPhone notifications without creating an extra SMS or iMessage.
It receives the text iOS makes available, subject to notification settings.

## Get started

Use the [single-board setup guide](docs/SINGLE_BOARD.md). There is one image,
`single-merged.bin`, and both devices pair with **DashBridge**. USB console
commands open iPhone and Tesla pairing separately and report both connections.

The public default installer still serves the preserved two-board release.
This branch's installer is a separate preview until hardware testing passes.

## Current status

- Two-board v0.1.4: the user confirmed **DashBridge test** appeared on the Tesla.
- Single-board: internal routing, pairing isolation and protocol host tests pass.
- Single-board hardware: both notification connections report ready together;
  the Tesla test message appeared. Version 0.2.1-dev fixes an ANCS flag check
  that could discard new WhatsApp notifications. Real delivery needs a retest.
- Outgoing replies, audio/call relay and configurable app selection are not implemented.

A successful build is not evidence that the phone and car work together.
See [validation history](docs/VALIDATION.md) for the original two-board work.

## Develop

With ESP-IDF v5.5.1 activated:

```sh
bash tools/test.sh
python3 tools/test_build_scope.py
bash tools/build.sh single
```

Build only `single` for this prototype. The old A/B images are preserved in
[v0.1.4](https://github.com/thomasgregg/DashBridge/releases/tag/v0.1.4);
its source tag, firmware checksums, installer archive and rollback guide
remain available independently of this branch.

## License

DashBridge is MIT licensed. ESP-IDF and the ESP32 controller have their own
notices; see [third-party notices](third_party/README.md).
