# Two-board call relay — 0.3.0-call-alpha

**Development build. Not yet tested with two physical boards.** The second board
has not arrived. Do not replace a working adapter expecting calls or music to be
proven. The public installer continues to serve the single-board notification
prototype; the original two-board design is preserved in release **v0.1.4**.

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

The Tesla selects **DashBridge B** as its active phone. The iPhone connects to
**DashBridge A** for calls and notification sharing. This does not keep the iPhone
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
- Answer, reject/end and DTMF forwarded to the iPhone. Success is returned only
  after an iPhone AT result. A command times out without being retried; a timed-out
  phone connection is reset so a late result cannot acknowledge a newer command.
- Bidirectional **8 kHz, 16-bit mono call audio**, using the pinned SDK's internal
  CVSD codec and legacy PCM callbacks. Wideband speech is disabled on both boards.
- Separate control and audio links, bounded queues, CRC checks, per-connection
  audio tokens, command deduplication and a three-second peer heartbeat timeout.
- Reconnection attempts to the last successfully connected Classic peer with
  bounded backoff. Reconnection and audio routing still require physical tests.

Not implemented in this milestone:

- Music (A2DP), media controls/metadata (AVRCP), contacts/history (PBAP).
- Dialing or redialing from the Tesla. Start an outgoing test call on the iPhone.
- Siri, volume synchronization, call waiting/conferences or multiple active calls.
- Wideband speech, custom noise reduction, message replies or configurable apps.

The one-call list is derived from iPhone call indicators, not a complete CLCC
proxy. Do not use this build to evaluate multiparty behavior. Call audio quality,
clock drift, echo, interruption recovery and long-duration stability are unverified.
A successful compile does not establish working calls.

## Hardware and wiring

Two **original ESP32-WROOM-32, 4 MB** development boards are required. These
instructions do not apply to ESP32-S3/C3 or ESP8266. Flash the matching A/B images
from the same build. Mark the boards before wiring them.

With both boards unplugged, connect five short jumper wires:

| Board A | Board B | Purpose |
| --- | --- | --- |
| GND | GND | Common ground |
| GPIO17 (TX2) | GPIO16 (RX2) | Notifications and call controls A → B |
| GPIO16 (RX2) | GPIO17 (TX2) | Notifications and call controls B → A |
| GPIO25 | GPIO26 | Call audio A → B |
| GPIO26 | GPIO25 | Call audio B → A |

Control uses UART2 at 115200 baud; audio uses UART1 at 921600 baud. Use the **GPIO
numbers**, not connector positions. All signal pins are 3.3 V. Power each board by
USB; do not connect their 5 V/3.3 V supply pins together. Neither audio nor data is
sent through USB: USB powers each board and exposes its separate log console.

No microphone, speaker or audio converter is required for this digital relay.
Keep the wires short for the first bench/car test. The final enclosure, power
supply and long-term automotive installation are separate work.

## First physical test, parked

1. Install `phone-merged.bin` on A and `car-merged.bin` on B from the same
   **0.3.0-call-alpha** build. The old v0.1.4 images cannot carry call audio.
2. Wire the boards as above, then power both. Open each board's USB log console
   separately and send `status`. Each should detect the other board's call relay.
3. On A send `pair phone`. In iPhone Bluetooth settings select **DashBridge A**.
   Accept pairing and enable **Show Notifications** when offered. A needs both
   its Classic call connection and BLE notification connection. Pairing stays
   open until both are ready, or for at most 120 seconds. If necessary select the
   second DashBridge A entry for the other service; capture logs rather than
   guessing whether both links are present.
4. On B send `pair car`. Pair **DashBridge B** from the Tesla Bluetooth screen,
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
   the iPhone, select DashBridge A as its audio route if needed, and check both
   directions and end-call from the Tesla. Do not use an emergency number.
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

Use ESP-IDF **v5.5.1**. Both targets are rebuilt for this milestone because both
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

SDK references: [HFP client API](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32/api-reference/bluetooth/esp_hf_client.html),
[pinned HFP role/codec options](https://github.com/espressif/esp-idf/blob/v5.5.1/components/bt/host/bluedroid/Kconfig.in),
[pinned audio gateway implementation](https://github.com/espressif/esp-idf/blob/v5.5.1/components/bt/host/bluedroid/btc/profile/std/hf_ag/btc_hf_ag.c).
