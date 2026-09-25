# DashBridge setup

The current release is **0.5.1-alpha**, a two-board prototype for original
ESP32-WROOM-32 boards. Start with the [README](../README.md#get-started) for the
five-wire layout, installation methods, pairing, and the parked-car test
checklist. Install the matching Board A and Board B images from the same
[manifest](../dist/manifest.json).

| Device | Bluetooth name | Purpose |
| --- | --- | --- |
| Board A on iPhone | Dash Calls | Calls and iPhone music link |
| Board A on iPhone | Dash Messages | Notification sharing |
| Board B on Tesla | Dash Tesla | Tesla phone, messages, music, and phonebook |

With the companion app, keep it open and accept the **Dash Messages** pairing
and notification-sharing prompts; then pair **Dash Calls** in iPhone Settings
when the app asks. Without the app, enter `pair phone` on Board A's USB console,
pair **Dash Messages** first, allow notification sharing, and then pair
**Dash Calls** after it becomes visible. On Board B, enter `pair car`, pair Dash Tesla with
the car, and enable message/contact syncing where offered. Each pairing window
lasts 120 seconds. Use `status` on both boards to check actual profile states;
`Dash Messages` need not show “Connected” in iPhone Settings for ANCS to be ready.

The native iPhone companion app is currently an internal TestFlight build. It
finds Board A, shows its connection status, and lets you choose apps that iOS
returns in its installed-app list. It does not need to stay open for forwarding.
The [USB setup page](https://thomasgregg.github.io/DashBridge/setup.html) remains
available for pairing controls, connection checks, and a Tesla test message.
Use Chrome or Edge on a computer with a USB data connection to a board. Its
one-minute app-discovery fallback learns a new app after a fresh notification;
notification text is requested only for allowed apps. Settings stay on Board A.
The complete interactive and browser protocol is documented in the
[USB command reference](USB_COMMANDS.md).

The Tesla selects Dash Tesla as its active phone. The separate iPhone-to-Tesla
phone-key pairing remains in place. Do all tests while parked.

## Installation and update layout

The full `dist/phone-full.bin` and `dist/car-full.bin` images are flashed at
`0x0`. A full-image installation can erase saved Bluetooth pairings. For an
update to already provisioned boards with the **same partition layout**, the
matching `build/phone/dashbridge.bin` or `build/car/dashbridge.bin` application
can be flashed at `0x10000` to preserve pairings. Never interchange full and
application-only images or flash an application-only image at `0x0`.

The [release notes](releases/0.5.1-alpha.md) separate software checks from
hardware observations. These exact 0.5.1-alpha images have not yet been
installed or checked end to end. Earlier applications booted and linked, but
Tesla delivery and reconnection still need a parked-car test.
In the previous development-board test, iPhone phonebook and call-history
pulls returned zero records; music sound, Tesla controls, and clear call audio
remain unverified.
