# Two-board call relay — 0.3.3-alpha

**Development build with unresolved audio problems.** Both boards have been
installed and tested with iPhone and Tesla. WhatsApp text delivery worked;
answered calls sounded robotic or unclear. This release adds
[tone and loopback diagnostics](AUDIO_ISOLATION_TESTS.md), with listening tests
still pending. Install both roles using the
[public installer](https://thomasgregg.github.io/DashBridge/).

This branch starts from the working single-board notification code, including the
ANCS flag and Tesla SDP fixes. It restores two physical Bluetooth radios and adds
the first call-relay milestone. It is an independent implementation; NotifyDrive's
internal wiring and firmware are not available to us.

## What changes

```text
                 Board A                    Board B
  iPhone ← HFP → headset   ← wired PCM →   phone gateway ← HFP → Tesla
         → ANCS → filter  → wired data →   message server → MAP →
```

The Tesla selects **Dash Tesla** as its active phone. The iPhone connects to
**Dash Calls** for calls and **Dash Messages** for notification sharing. This does not keep the iPhone
as the Tesla's direct active phone; the accepted direction is to relay its
functionality through the adapter. Leave the Tesla phone key in place.

The message-only experiment did not solve coexistence: selecting DashBridge Test
disconnected the iPhone, and selecting the iPhone stopped test notifications.
Two radios alone do not change Tesla's active-phone policy. The new work is the
actual headset/audio-gateway relay between those radios.

## First milestone

Implemented for hardware testing:

- A phone-facing HFP hands-free client alongside the existing ANCS receiver.
- A Tesla-facing HFP audio gateway alongside the existing MAP message server.
- iPhone call/network/battery indicators, incoming caller number when available,
  and a one-call list shown to the car.
- Answer, reject/end, explicit-number dialing and DTMF forwarded to the iPhone. Success is returned only
  after an iPhone AT result. A command times out without being retried; a timed-out
  phone connection is reset so a late result cannot acknowledge a newer command.
- Bidirectional **16 kHz, 16-bit mono call audio**, using ESP-IDF v5.5.5 internal
  mSBC codecs. Each Bluetooth side can fall back to 8 kHz CVSD; filtered conversion
  keeps the wired link at 16 kHz even when the negotiated codecs differ.
- Separate control and audio links, bounded queues, CRC checks, per-connection
  audio tokens, command deduplication and a three-second peer heartbeat timeout.
- Board A initiates iPhone HFP reconnection with bounded backoff. Board B waits
  for Tesla to initiate; it does not make outgoing HFP attempts. iPhone calls and
  notifications reconnected after updates. Tesla-owned vehicle-wake reconnect
  was verified in an earlier B build; broader reliability remains unproven.
- Temporary per-side test tone and attenuated loopback, controlled from either
  USB console, with a 60-second timeout and automatic stop on audio disconnection.
  Both boards report Bluetooth and wired-audio diagnostics.

Not implemented in this milestone:

- Music (A2DP), media controls/metadata (AVRCP), contacts/history (PBAP).
- Redial, memory dialing and VoIP-specific dialing. Start the first outgoing
  test call on the iPhone before trying the Tesla dial pad.
- Siri, volume synchronization, call waiting/conferences or multiple active calls.
- Custom noise reduction, message replies or configurable apps.

The one-call list is derived from iPhone call indicators, not a complete CLCC
proxy. Do not use this build to evaluate multiparty behavior. Call audio quality is currently unacceptable in user tests. Clock drift, echo,
interruption recovery and long-duration stability require further investigation.
A successful compile does not establish working calls. See [HD audio validation](HD_AUDIO.md)
for the current build and remaining hardware checks.

## Hardware and wiring

Two **original ESP32-WROOM-32, 4 MB** development boards are required. These
instructions do not apply to ESP32-S3/C3 or ESP8266. Flash the matching A/B images
from the same build. Mark the boards before wiring them.

### ESP32 D1 Mini pin locations

![ESP32 D1 Mini wiring: five connections between A and B](assets/d1-mini-wiring.svg)

Both boards are shown from above, with the metal module visible and USB at the
bottom. Connect matching numbered circles. The numbers 1–5 identify the wires;
the GPIO labels identify the pins. Wire colours are only a visual aid.

The drawing follows the [AZDelivery ESP32 D1 Mini pinout](https://cdn.shopify.com/s/files/1/1509/1638/files/D1_Mini_ESP32_-_pinout.pdf?v=1604068668).
It does not apply to the ESP8266 D1 Mini. For other ESP32 boards, follow the GPIO
labels rather than these physical positions.

Count pins from the antenna end (top): GPIO26 is left inner, third; GPIO25 is
right outer, third. GPIO17, GPIO16 and the pictured GND are right inner, fifth,
sixth and seventh respectively. Fit soldered headers before using jumper leads.

### ESP32 Dev Kit C V2 pin locations

![AZDelivery ESP32 Dev Kit C V2 wiring: five connections between A and B](assets/devkit-c-v2-wiring.svg)

This illustration follows the [AZDelivery 38-pin Dev Kit C V2 pinout](https://cdn.shopify.com/s/files/1/1509/1638/files/ESP-32_NodeMCU_Developmentboard_Pinout.pdf?v=1609851295)
for ASIN B074RGW2VQ. Both boards are viewed from above, with the antenna at the
top and USB at the bottom. Each side has 19 pins. Count from the top:

- GPIO25: left side, ninth pin.
- GPIO26: left side, tenth pin.
- GND shown: left side, fourteenth pin.
- GPIO17: right side, eleventh pin.
- GPIO16: right side, twelfth pin.

EN/RST restarts the board; BOOT is used to enter programming mode. Neither
button needs an external wire. Check the printed labels before connecting.

The D1 Mini and Dev Kit C V2 can be mixed: use the appropriate A/B illustration
for each board's firmware role and connect matching circled wire numbers.
The GPIO connections below are the same for both board layouts.

### Connect the five wires

With both boards unplugged, connect five short jumper wires:

| Board A | Board B | Purpose |
| --- | --- | --- |
| GND | GND | Common ground |
| GPIO17 (TX2) | GPIO16 (RX2) | Notifications and call controls A → B |
| GPIO16 (RX2) | GPIO17 (TX2) | Notifications and call controls B → A |
| GPIO25 | GPIO26 | Call audio A → B |
| GPIO26 | GPIO25 | Call audio B → A |

Control uses UART2 at 115200 baud; audio uses UART1 at 460800 baud. Use the **GPIO
numbers**, not connector positions. All signal pins are 3.3 V. Power each board by
USB; do not connect their 5 V/3.3 V supply pins together. Neither audio nor data is
sent through USB: USB powers each board and exposes its separate log console.

No microphone, speaker or audio converter is required for this digital relay.
Keep the wires short for the first bench/car test. The final enclosure, power
supply and long-term automotive installation are separate work.

## First physical test, parked

1. Open the [installer](https://thomasgregg.github.io/DashBridge/) in Chrome or
   Edge on a computer. Connect one board at a time and select **A — iPhone** or
   **B — Tesla**. Install `phone-merged.bin` on A and `car-merged.bin` on B from
   the same **0.3.3-alpha** release.
2. Wire the boards as above, then power both. Open each board's USB log console
   separately and send `status`. Each should detect the other board's call relay.
3. On A send `pair phone`. In iPhone Bluetooth settings pair **Dash Calls**
   and **Dash Messages**. Accept the pairing prompts and enable **Share System
   Notifications** for Dash Messages when offered. A needs both connections.
   Pairing stays open until both are ready, or for at most 120 seconds.
4. On B send `pair car`. Pair **Dash Tesla** from the Tesla Bluetooth screen,
   enable message syncing and select B as the active phone. The iPhone's direct
   Tesla call connection is expected to be replaced. Do not remove its phone key.
5. Send `status` to both boards. Look for **Call profile: ready; other board:
   ready**, and both notification services ready. Send `test` on B, then have
   someone send a fresh WhatsApp message while the iPhone is locked.
6. Have someone call the iPhone. Check the Tesla rings. Answer on the Tesla and
   confirm **both directions**: hear the caller through the car speakers, and
   have the caller confirm they hear the Tesla microphone. Confirm audio status
   shows incoming PCM packets on both boards. End using the Tesla.
7. Repeat but reject the incoming call. Then start an ordinary outgoing call on
   the iPhone, select Dash Calls as its audio route if needed, and check both
   directions and end-call from the Tesla. Then try one explicit number from the
   Tesla dial pad. Check that it calls only once. Do not use an emergency number.
8. During a test call, disconnect one inter-board link at a time. Verify no stale
   speech plays on reconnection, no call repeats, and the phone remains usable.
   Loss of the control heartbeat requests audio disconnection on A; automatic
   iPhone audio-route recovery must be checked on hardware.
9. Restart both boards and check reconnection, new WhatsApp delivery and another
   two-way call. Leave long calls/music/real driving for later validation.

Save the logs from **both boards** with the step that failed and whether each
side could hear the other. Call audio and message text are not logged. SDK debug
logging can expose device identifiers; share only logs you intend to share.

## Build and verification

Use ESP-IDF **v5.5.5**. Both targets are rebuilt for this milestone because both
firmware roles change. Future rebuilds continue to select only stale targets.

```sh
bash tools/test.sh
python3 tools/test_build_scope.py
bash tools/build.sh both
```

Host tests cover malformed/fragmented messages, command eligibility and replay
rejection, audio frame corruption/recovery, buffering, and the existing ANCS/MAP
regressions. CI compiles both actual ESP32 targets and tests the Tesla SDP patch.
Bluetooth timing, speech quality and car/phone interoperability require hardware.
See the [recorded build and test results](CALL_RELAY_VALIDATION.md).

SDK references: [HFP client API](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32/api-reference/bluetooth/esp_hf_client.html),
[pinned HFP role/codec options](https://github.com/espressif/esp-idf/blob/v5.5.5/components/bt/host/bluedroid/Kconfig.in),
[pinned audio gateway implementation](https://github.com/espressif/esp-idf/blob/v5.5.5/components/bt/host/bluedroid/btc/profile/std/hf_ag/btc_hf_ag.c).
