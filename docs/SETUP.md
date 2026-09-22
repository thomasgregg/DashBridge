# DashBridge setup

The current release is **0.4.0-alpha**, a two-board prototype for original
ESP32-WROOM-32 boards. Start with the [README](../README.md#get-started) for the
five-wire layout, installation methods, pairing, and the parked-car test
checklist. Install the matching Board A and Board B images from the same
[manifest](../dist/manifest.json).

| Device | Bluetooth name | Purpose |
| --- | --- | --- |
| Board A on iPhone | Dash Calls | Calls and iPhone music link |
| Board A on iPhone | Dash Messages | Notification sharing |
| Board B on Tesla | Dash Tesla | Tesla phone, messages, music, and phonebook |

On Board A's USB console, enter `pair phone`, then pair both iPhone devices and
allow notification sharing. On Board B, enter `pair car`, pair Dash Tesla with
the car, and enable message/contact syncing where offered. Each pairing window
lasts 120 seconds. Use `status` on both boards to check actual profile states;
`Dash Messages` need not show “Connected” in iPhone Settings for ANCS to be ready.

The Tesla selects Dash Tesla as its active phone. The separate iPhone-to-Tesla
phone-key pairing remains in place. Do all tests while parked.

## Installation and update layout

The full `dist/phone-merged.bin` and `dist/car-merged.bin` images are flashed at
`0x0`. A full-image installation can erase saved Bluetooth pairings. For an
update to already provisioned boards with the **same partition layout**, the
matching `build/phone/dashbridge.bin` or `build/car/dashbridge.bin` application
can be flashed at `0x10000` to preserve pairings. Never interchange merged and
application-only images or flash an application-only image at `0x0`.

The [release notes](releases/0.4.0-alpha.md) separate host-tested features from
hardware observations. In the most recent development-board test, iPhone
phonebook and call-history pulls returned zero records; music sound, Tesla
controls, and clear call audio remain unverified. The release images themselves
have not yet been installed for a full hardware test.

The original one-board prototype is historical. Its source and instructions
remain in the [`archive/single-board` branch](https://github.com/thomasgregg/DashBridge/tree/archive/single-board),
not in this release's downloads.
