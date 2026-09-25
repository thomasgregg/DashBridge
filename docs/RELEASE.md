# Current DashBridge release: 0.5.2-alpha

This is the current two-board prerelease for original ESP32-WROOM-32 boards.
It includes notifications, calls, encoded call audio, music and media controls,
contacts and call lists, staged pairing, deterministic reconnect coordination,
the browser USB setup flow, and the iOS Setup GATT v1 contract.

## Firmware images

| Board | Complete image | Embedded version | SHA-256 |
| --- | --- | --- | --- |
| A — iPhone | `phone-full.bin` | `0.5.2-alpha+3a049933a55a` | `e99afaf151196d4a9d60e5b324f494dcfd5534859e0aacbe080e050b6cf1008f` |
| B — Tesla | `car-full.bin` | `0.5.2-alpha+1067a548d951` | `fb5b84b3eec93530691087d893ae1d6d669f05cd27dcf3e56390cb3624cdbd44` |

The complete images contain the bootloader, partition table, and application
and are flashed at `0x0`. They may erase Bluetooth bonds and app choices.
Application-only images are also included for `0x10000` updates when the
installed partition layout is known to match. Install both board roles from
the same release.

## Validation status

Both images build with pinned ESP-IDF v5.5.5. Architecture boundaries,
protocol bounds, malformed input, setup compatibility, notification state,
call and music control, encoded media transport, audio timing and isolation,
contacts, pairing order, reconnect ownership, persistence, replay protection,
browser integrity, production Bluetooth configuration, and SDP checks pass.

These exact images have **not** been tested end to end with an iPhone and
Tesla. The [manifest](../dist/manifest.json) therefore records
`hardware_tested: false` for both boards. Physical testing must confirm
notification delivery, phonebook synchronization, call controls and audible
quality, music playback and controls, and sustained reconnection.

Use the [setup guide](SETUP.md) and the
[parked-car checklist](../README.md#parked-car-test-checklist). Keep the
existing direct-phone option until the bridge has been verified in your car.
