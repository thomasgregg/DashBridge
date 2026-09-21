<p align="center">
  <img src="docs/assets/dashbridge-banner.svg" alt="DashBridge — WhatsApp notifications for your Tesla." width="100%" />
</p>

<p align="center">
  <a href="https://github.com/thomasgregg/DashBridge/actions/workflows/ci.yml"><img src="https://github.com/thomasgregg/DashBridge/actions/workflows/ci.yml/badge.svg" alt="CI status" /></a>
  <img src="https://img.shields.io/badge/status-experimental-7c7154?style=flat-square&labelColor=102c37" alt="Status: experimental" />
  <img src="https://img.shields.io/badge/hardware-ESP32--WROOM--32-176c59?style=flat-square&labelColor=102c37" alt="Hardware: original ESP32-WROOM-32" />
  <img src="https://img.shields.io/badge/ESP--IDF-v5.5.1-3b5d66?style=flat-square&labelColor=102c37" alt="ESP-IDF v5.5.1" />
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-176c59?style=flat-square&labelColor=102c37" alt="License: MIT" /></a>
</p>

<p align="center">
  <a href="#get-started">Get started</a> ·
  <a href="#how-it-works">How it works</a> ·
  <a href="#project-status">Project status</a> ·
  <a href="docs/CALL_RELAY.md">Setup guide</a> ·
  <a href="CONTRIBUTING.md">Contribute</a>
</p>

# DashBridge

**Your iPhone notifications, on your Tesla dashboard.**

Two ESP32 boards connect your iPhone to the Tesla’s Bluetooth message interface. DashBridge forwards new WhatsApp notifications locally—without sending an SMS, signing in to WhatsApp, or routing message content through a server. The new call relay adds phone controls and audio between the same two boards.

> [!IMPORTANT]
> **Call relay alpha · v0.3.2.** Both board images compile and software tests pass. Board B's Tesla wake reconnect, call profile and message sync are hardware verified; the complete two-board call/audio relay still needs physical testing. Music, contacts and replies are not implemented. The Tesla uses **DashBridge B as its active phone**, with iPhone calls relayed through Board A. See the [test results](docs/CALL_RELAY_VALIDATION.md) and [setup guide](docs/CALL_RELAY.md).

## Why DashBridge?

The project started with a simple question: can the car's existing message interface display WhatsApp notifications without forwarding them as real SMS messages?

DashBridge explores that through a small, inspectable hardware bridge:

- **Local message transport.** The boards use Bluetooth and a wired connection between them. They do not upload message content.
- **No extra phone messages.** Your original WhatsApp notification remains on the iPhone; DashBridge creates no duplicate SMS or iMessage.
- **No WhatsApp credentials.** Notification access comes from iOS through Apple's notification service, without a linked-device login.
- **Small hardware footprint.** Two original ESP32 development boards, five jumper wires, and USB power.
- **Source you can inspect.** Firmware, protocol handling, tests, build tools, and setup documentation live in this repository.

## How it works

```mermaid
flowchart TB
    phone["iPhone"]
    receiver["DashBridge A<br/>iPhone connection"]
    gateway["DashBridge B<br/>Tesla connection"]
    car["Tesla"]

    phone <-->|"Bluetooth · calls + notifications"| receiver
    receiver <-->|"Wired data + audio · 5 connections"| gateway
    gateway <-->|"Bluetooth · calls + messages"| car
```

**Board A** receives iPhone notifications through Apple ANCS, filters for WhatsApp and WhatsApp Business, and acts as a Bluetooth headset for calls. **Board B** presents a phone and message inbox to the Tesla. Separate wired links carry call audio and message/control data between the boards.

The single-board design delivered WhatsApp notifications but displaced the iPhone’s active phone connection. A message-only experiment did not solve that. This two-board design adds the call relay so the iPhone’s calls can pass through DashBridge. Call behavior and audio quality remain unverified on hardware.

The Tesla phone key stays paired directly with the iPhone. DashBridge does not change that pairing. Physical coexistence and reconnection still need testing.

See the [call-relay guide](docs/CALL_RELAY.md) for the architecture and test sequence, and the [notification implementation notes](docs/DEVELOPMENT.md) for ANCS and MAP details.

## Project status

| Area | Current state |
| :--- | :--- |
| Firmware builds | Both original-ESP32 targets compile with ESP-IDF v5.5.1 |
| Host tests | Notification, call-control and audio-transport tests pass with sanitizers |
| WhatsApp delivery | Confirmed on the earlier single-board prototype; new two-board path needs testing |
| Call controls | Answer, reject/end, explicit-number dialing and DTMF implemented |
| Call audio | Two-way narrowband audio transport implemented; **not hardware-tested** |
| Reconnection and daily reliability | **Not hardware-tested** on this design |
| Music, media controls, contacts and replies | **Not implemented** |

“Implemented” means present in the code, not verified on a device. See the [current validation record](docs/CALL_RELAY_VALIDATION.md) and [earlier hardware reports](docs/VALIDATION.md).

### Current boundaries

- This is a **one-call prototype**. Siri, redial, call waiting and conferences are not supported.
- The Tesla selects DashBridge B as its active phone. It does not keep the iPhone as a second active phone for calls.
- Only new notifications received while the car connection is ready are forwarded. There is no offline backlog or replay of old messages.
- The inbox holds up to **32 notifications**, with up to **768 UTF-8 bytes** of message body per notification.
- Repeated adds and edits to a retained notification ID update its entry without another alert. iOS grouping can affect how many alerts reach the car.
- iPhone previews, Focus settings, locking and app behavior affect the available notification content. Direct pairing in iPhone Settings worked with the earlier prototype; nRF Connect was not needed.

## Hardware

The initial test target is an **iPhone and an approximately 2021 Tesla Model 3**. Notifications have worked on that setup; the new call relay has not been tested yet.

| Part | Quantity | Notes |
| :--- | :---: | :--- |
| Original ESP32-WROOM-32 development board | 2 | 4 MB flash; for example, AZDelivery DevKit C V2 |
| USB data cable | As needed | Match the board connector; programming needs a data-capable cable |
| USB power connections | 2 | Power each board independently |
| Female-to-female jumper wire | 5 | Four signal wires and a shared ground |

> **Chip selection matters:** this firmware targets the original `esp32`. ESP32-S2, S3, and C3 boards are not substitutes for the car-facing board, which needs Bluetooth Classic.

### Wiring

Disconnect power before wiring. Label the boards **A — iPhone** and **B — Tesla**.

![ESP32 D1 Mini wiring: five connections between A and B](docs/assets/d1-mini-wiring.svg)

Shown from above, USB sockets at the bottom. This layout matches the **AZDelivery ESP32 D1 Mini**. Connect matching numbered pins; the GPIO numbers are labelled beside them.

![ESP32 Dev Kit C V2 wiring: five connections between A and B](docs/assets/devkit-c-v2-wiring.svg)

For the **AZDelivery ESP32 Dev Kit C V2 (38 pins, ASIN B074RGW2VQ)**, use this second illustration. It follows the [manufacturer's pinout](https://cdn.shopify.com/s/files/1/1509/1638/files/ESP-32_NodeMCU_Developmentboard_Pinout.pdf?v=1609851295). The two buttons are **EN/RST** (restart) and **BOOT** (programming). Both illustrations use the same wire numbers and GPIO connections, so a D1 Mini and a Dev Kit C V2 can also be paired: use the A view for the board running A firmware and the B view for the board running B firmware. Follow the printed GPIO labels, not the physical positions from the other board's illustration.

| DashBridge A | DashBridge B |
| :--- | :--- |
| GPIO **17** · TX2 | GPIO **16** · RX2 |
| GPIO **16** · RX2 | GPIO **17** · TX2 |
| GPIO **25** · audio TX | GPIO **26** · audio RX |
| GPIO **26** · audio RX | GPIO **25** · audio TX |
| **GND** | **GND** |

Power both boards over USB. Do **not** connect their 5V, VIN, or 3V3 pins together. Use short wires and follow the [full wiring guide](docs/CALL_RELAY.md#hardware-and-wiring).

## Get started

### 1. Install from your browser

[![Install DashBridge](docs/assets/install-button.svg)](https://thomasgregg.github.io/DashBridge/)

Use **Chrome or Edge on a computer**. Connect one board with a USB data cable, select **A — iPhone** or **B — Tesla**, click **Install**, and select its USB port. The page selects the correct firmware and settings for you. Repeat for the other board. Installation clears the selected board’s saved pairings.

Prefer a manual installation? [Download the project ZIP](https://github.com/thomasgregg/DashBridge/archive/refs/heads/main.zip), or clone the repository:

```sh
git clone https://github.com/thomasgregg/DashBridge.git
cd DashBridge
```

The [`dist/`](dist/) directory contains the prebuilt prototype images:

| Board | Firmware | Bluetooth name |
| :--- | :--- | :--- |
| **A — iPhone** | [`phone-merged.bin`](dist/phone-merged.bin) | `DashBridge A` |
| **B — Tesla** | [`car-merged.bin`](dist/car-merged.bin) | `DashBridge B` |

The [manifest](dist/manifest.json) records image and source checksums. Board B's Tesla wake reconnect, HFP profile, MAP transport and message notifications have been hardware tested. The complete two-board call/audio relay remains an alpha until Board A and wired audio are tested together.

### 2. Wire and pair the boards

Unplug both boards and connect the five wires shown above. Power them again, pair **DashBridge A** with the iPhone, then pair **DashBridge B** with the Tesla and enable message syncing. The [setup guide](docs/CALL_RELAY.md#first-physical-test-parked) covers the pairing commands and notification permissions.

### 3. Test messages, then a call

While parked, open **Logs & Console** for Board B and enter `test`. A message from **DashBridge test** should appear in the Tesla. Then test a new WhatsApp notification.

For the first call test, have someone call the iPhone. Answer on the Tesla and check that you can hear them through the speakers **and they can hear the Tesla microphone**. Save logs from both boards if either direction fails. See the [complete test sequence](docs/CALL_RELAY.md#first-physical-test-parked).

## Build from source

Use **ESP-IDF v5.5.5** targeting the original `esp32`. The pinned SDK revision is [`b774170`](https://github.com/espressif/esp-idf/tree/b774170ff46c393eeb5e495ea37936038d3f4f4f).

After [installing ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32/get-started/index.html) and activating its environment, run from the repository root:

```sh
# Host tests: requires Clang with AddressSanitizer and UndefinedBehaviorSanitizer.
bash tools/test.sh

# Build and package firmware for both boards.
bash tools/build.sh

# Or build one board only.
bash tools/build.sh phone
bash tools/build.sh car
```

Build outputs stay in `build/`. Packaged flash images and their manifest are written to `dist/`. The [CI workflow](.github/workflows/ci.yml) runs host tests and builds only stale firmware roles; a green check does not establish hardware compatibility.

## Privacy and notification behavior

DashBridge keeps notification content in RAM and on the local Bluetooth/UART links. It does not write message bodies to its logs or flash storage. Bluetooth bonding keys are stored on the boards so paired devices can reconnect.

ANCS event headers do not identify the source app, so Board A must request notification attributes before it can filter for WhatsApp. Attributes from other apps are not forwarded to Board B.

Disconnects clear the adapter's inbox. The Tesla may maintain its own cache; clearing a board cannot guarantee deletion of that cache. DashBridge also does not dismiss or silence the original WhatsApp notification on your iPhone.

## Roadmap

- [x] Implement local notification filtering and the Tesla message gateway.
- [x] Display a test message and a real WhatsApp notification on the Tesla.
- [x] Add the two-board call controls and audio transport.
- [x] Compile both call-relay images and pass software checks.
- [ ] Test notifications and two-way call audio with both physical boards.
- [ ] Validate power cycles, reconnection, audio quality and phone-key coexistence.
- [ ] Add music and media controls.
- [ ] Add contacts and call history.
- [ ] Add configurable notification app selection.

Hardware test results will determine the next changes.

## Contributing

The most valuable contribution right now is a reproducible **hardware test report**. If you have the matching boards and are comfortable with firmware experiments, follow the parked-car test sequence and [report your result](https://github.com/thomasgregg/DashBridge/issues/new?template=hardware-report.yml).

Protocol fixes, parser tests, and documentation improvements are welcome too. Read [CONTRIBUTING.md](CONTRIBUTING.md) for the development workflow and the details to include with a report.

## Documentation

| Guide | What you will find |
| :--- | :--- |
| [Setup](docs/CALL_RELAY.md) | Flashing, pairing, wiring, test sequence, and reset |
| [Implementation](docs/DEVELOPMENT.md) | ANCS, MAP subset, wire format, and state handling |
| [Validation](docs/CALL_RELAY_VALIDATION.md) | Current software evidence and outstanding hardware tests |
| [Sources](docs/SOURCES.md) | Specifications, upstream references, and project provenance |
| [Contributing](CONTRIBUTING.md) | Development workflow and useful hardware reports |

## License and acknowledgments

DashBridge's original source and documentation are available under the [MIT License](LICENSE). ESP-IDF and its components retain their own licenses; see [third-party notices](third_party/README.md).

Built with [Espressif ESP-IDF](https://github.com/espressif/esp-idf) and Apple's [ANCS specification](https://developer.apple.com/library/archive/documentation/CoreBluetooth/Reference/AppleNotificationCenterServiceSpecification/Specification/Specification.html). NotifyDrive's public prototype discussion helped inspire the investigation; DashBridge is an independent implementation, not its published source.

DashBridge is not affiliated with or endorsed by Tesla, Apple, WhatsApp, Meta, or NotifyDrive. Product names belong to their respective owners.
