# Bluetooth audio diagnostics — 21 September 2026

The UART timing build removed measured wire sequence gaps, CRC errors and UART
overruns in the latest call. The user still reported bad/robotic audio, possibly
worse. No successful audio result should be inferred from the clean wire counters.

This build retains the tested 460800-baud transport and audio behavior. It adds
read-only ESP-IDF SCO packet-statistics requests every five seconds during an
active audio connection, using the connection handle from the HFP audio event.
Both roles handle their packet-statistics callbacks and share the result over
the existing control channel. Either USB console prints:

- local Bluetooth receive total/correct/error/missing/partially-lost counts;
- local transmit total/discarded counts;
- the peer board's same counters and the age of each snapshot;
- the negotiated SCO handle and preferred frame size at audio connection.

Counters are last observed snapshots, not permanent per-device totals: the SDK
resets SCO statistics for new connections. A last snapshot can precede call end
by up to five seconds. Controller receive status cannot by itself establish the
correctness of decoded speech or rule out an mSBC codec/packetization problem.

Both firmware builds and the existing sanitizer/configuration tests pass. This
change itself is diagnostic and does not claim to fix the audible problem.

Related open reports, consulted as background rather than proof of our cause:

- https://github.com/espressif/esp-idf/issues/17925 — HFP mSBC glitches with other
  UART/BLE work on ESP-IDF 5.5.1.
- https://github.com/espressif/esp-idf/issues/18865 — mSBC distortion with known
  PCM input on a different ESP-IDF version and a Windows audio gateway.

No dependency upgrades or vendor-stack patches were made based on these reports.

## Current installation

These counters are installed on both boards as part of the latest
[tone/loopback diagnostic pair](AUDIO_ISOLATION_TESTS.md). Both started in normal
audio mode and remote stop was acknowledged. A new answered-call capture with
these counters is still pending; no radio-link cause has been established.
