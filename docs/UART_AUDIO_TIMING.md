# UART audio timing investigation — 21 September 2026

> **Investigation record.** The post-install result at the end supersedes the initial pending-test notes. The 460800-baud change remains in current firmware; audible quality is still unresolved. See [current status](CALL_RELAY_VALIDATION.md).

The previous buffer-fix test confirmed mSBC at 16 kHz on both boards, nonzero
microphone input, and persistent inter-board frame loss. Settled totals were:

| Direction | Sent | Received | Difference |
| --- | ---: | ---: | ---: |
| A to B, caller audio | 3130 | 2742 | 388 (12.4%) |
| B to A, Tesla microphone | 3121 | 2607 | 514 (16.5%) |

A few frames can be discarded at stream start/teardown. However, receive sequence
gaps increased repeatedly during the call; neither transmit queue-full/stale
counts nor playback overflow explained them. The old build lacked UART and CRC
error counters, so it did not distinguish electrical corruption from receiver
FIFO overrun or other transport loss.

This build tests three related changes to receive timing:

- Reduce full-duplex audio UART baud from 921600 to 460800. A 256-byte frame now
  occupies approximately 5.56 ms of the 7.5 ms audio period. A compile-time check
  verifies sufficient wire capacity. The PCM remains 16 kHz; this is not a codec
  or sample-rate downgrade.
- Trigger the RX FIFO interrupt at 64 bytes instead of relying on the driver's
  near-full default. This leaves more room for incoming data while Bluetooth is
  busy. The 2048-byte driver buffer is retained.
- Calculate transmit frame CRC outside the interrupt-masking audio critical
  section. PCM and stream tokens are copied together under the lock; the worker
  still rejects stale tokens before transmission.

New counters report UART FIFO/ring overruns, framing/parity errors, and invalid
frame CRCs. They are included in the existing peer diagnostic snapshots, allowing
either USB console to inspect both boards. No audio samples are logged.

Both boards must run the same audio UART baud. Keep them out of calls while
updating the pair. Call/control protocol v2 and the 16 kHz frame format are
unchanged; notification routing and pairing storage are preserved.

Both ESP32 builds, configuration checks, source/image checksum checks, and the
sanitizer test suite pass. The suite includes CRC rejection/counting, protocol
resynchronization, rate conversion, and SDK drain-until-empty callback behavior.
A fresh physical call must establish whether frame loss and sound quality improve;
these code/build checks do not establish an audible fix.

## Installed pair

Both applications were flashed with hardware identity checks and flash hash
verification, preserving pairing storage:

- A: `0.3.2-alpha+b2e8d82cb88f`
- B: `0.3.2-alpha+9b42b3208dea`

Both consoles' shared counters show `baud=460800`. A's HFP and ANCS reconnect
automatically. B was waiting for Tesla at the startup check. Physical call
validation is pending.

## Post-install call result

The user reports the call was bad, perhaps worse. During the captured call both
boards used mSBC at 16 kHz. Both reported zero receive sequence gaps, zero wire
CRC errors, zero UART errors, zero queue drops, and zero playback overflows.
The physical wire transport problem no longer appears in this test, but the
audible problem remains. Next diagnostics query each Bluetooth SCO link's
received-correct/error/missing/lost and transmitted-discarded packet counters.
