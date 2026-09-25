# DashBridge firmware 0.5.3-alpha

This two-board prerelease is for original ESP32-WROOM-32 boards. It includes
notifications, calls, encoded call audio, music and media controls, contacts
and call lists, staged pairing, deterministic reconnect coordination, the
browser USB setup flow, and Setup GATT v1 support for the iPhone app.

## Firmware images

| Board | Complete image | Embedded version | SHA-256 |
| --- | --- | --- | --- |
| A — iPhone | `phone-full.bin` | `0.5.3-alpha+a228d0702f52` | `2a89b34f24b322cf1679d1a4023e65e46d1dd55f8a04b0e6f9cc21c13a216fa7` |
| B — Tesla | `car-full.bin` | `0.5.3-alpha+5c453ca7f354` | `621263229076d30cbf3179d931d984bd78d138572624038093d22e95b2e09e5a` |

The complete images contain the bootloader, partition table, and application
and are flashed at `0x0`. They may erase Bluetooth bonds and app choices.
Application-only images are available at `0x10000` when the installed
partition layout is known to match. Install both board roles from this same
firmware release.

## App compatibility

This firmware implements Setup GATT v1 and is compatible with iOS app 1.0.
Firmware and app releases are independent: a firmware release does not require
a new app release while the Setup GATT contract remains compatible. See the
[compatibility matrix](../contracts/COMPATIBILITY.md).

## Validation status

Both images build with pinned ESP-IDF v5.5.5. Architecture boundaries,
protocol bounds, malformed input, setup compatibility, notification state,
call and music control, encoded media transport, audio timing and isolation,
contacts, pairing order, reconnect ownership, persistence, replay protection,
browser integrity, production Bluetooth configuration, and SDP checks pass.

These exact images have been flashed to the target boards and passed boot,
Bluetooth service-registration, inter-board-link, controlled outage/recovery,
command failure-path, and 45-second linked/discoverable stability checks, but
have **not** been tested end to end with an iPhone and Tesla. The
[manifest](../dist/manifest.json) therefore records `hardware_tested: false`
for both boards. Physical testing must confirm
notification delivery, phonebook synchronization, call controls and audible
quality, music playback and controls, and sustained reconnection.

Use the [setup guide](SETUP.md) and the
[parked-car checklist](../README.md#parked-car-test-checklist). Keep the
existing direct-phone option until the bridge has been verified in your car.
