# DashBridge firmware 0.5.3-alpha

This two-board prerelease is for original ESP32-WROOM-32 boards. It includes
notifications, calls, encoded call audio, music and media controls, contacts
and call lists, staged pairing, deterministic reconnect coordination, the
browser USB setup flow, and Setup GATT v1 support for the iPhone app.

## Firmware images

| Board | Complete image | Embedded version | SHA-256 |
| --- | --- | --- | --- |
| A — iPhone | `phone-full.bin` | `0.5.3-alpha+10e32023240b` | `49b81c7cd6c8a508591b46297b9d5430de3274137ed7adc8b03b5079646db4ee` |
| B — Tesla | `car-full.bin` | `0.5.3-alpha+689d47c3c548` | `d19a038170821a3707347dfcc9ad22bdbd9ec6984b7eab9a66c8b3fcc2489a8c` |

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

The secure BLE and Classic Bluetooth pairing changes passed a live Board A
pairing trace. These exact images were hash-verified while flashing the target
boards, booted with the expected embedded versions, and established the board
link. Board A also reported active BLE advertising and was independently found
as **DashBridge** by a Bluetooth scan. They have **not** yet completed the full
iPhone and Tesla release check, so the [manifest](../dist/manifest.json)
records `hardware_tested: false` for both boards. Physical testing must confirm
notification delivery, phonebook synchronization, call controls and audible
quality, music playback and controls, and sustained reconnection.

Use the [setup guide](SETUP.md) and the
[parked-car checklist](../README.md#parked-car-test-checklist). Keep the
existing direct-phone option until the bridge has been verified in your car.
