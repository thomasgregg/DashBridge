# DashBridge firmware 0.5.3-alpha

This two-board prerelease is for original ESP32-WROOM-32 boards. It includes
notifications, calls, encoded call audio, music and media controls, contacts
and call lists, staged pairing, deterministic reconnect coordination, the
browser USB setup flow, and Setup GATT v1 support for the iPhone app.

## Firmware images

| Board | Complete image | Embedded version | SHA-256 |
| --- | --- | --- | --- |
| A — iPhone | `phone-full.bin` | `0.5.3-alpha+a151619c299d` | `b163d9f8103a0057be7759262ff58bd40e14c776d6bfcf3df961c403f3d98ffb` |
| B — Tesla | `car-full.bin` | `0.5.3-alpha+aca88d9d4637` | `662337db3528576683f03bcd192b0d55bd0d8cef8a475ef5e3730155ce63fb8f` |

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

Earlier secure BLE and Classic Bluetooth pairing changes passed a live Board A
pairing trace. These newly built images add a pairing-window refresh for the
app's Classic transport-bridging request and have not yet been flashed or
hardware-tested. The [manifest](../dist/manifest.json) therefore records
`hardware_tested: false` for both boards. Physical testing must confirm the
one-step iPhone pairing flow, notification delivery, phonebook synchronization,
call controls and audible quality, music playback and controls, and sustained
reconnection.

Use the [setup guide](SETUP.md) and the
[parked-car checklist](../README.md#parked-car-test-checklist). Keep the
existing direct-phone option until the bridge has been verified in your car.
