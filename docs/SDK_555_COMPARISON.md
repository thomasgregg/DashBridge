# Board B: ESP-IDF 5.5.5 comparison

This branch builds the Tesla board only for a controlled comparison. The main installer and Board A image remain on their existing release. ESP-IDF is pinned to v5.5.5, commit `b774170ff46c393eeb5e495ea37936038d3f4f4f`, including the controller shipped with that release.

## Evidence and scope

The 2026-09-20 trace from B version `0.3.1-alpha+d460f0158324` showed eight outgoing ACL links closing with remote reason 0x13 before authentication or an SDP exchange. Bonds remained intact; Tesla-initiated HFP and MAP succeeded. The trace does not establish the remote device's reason or prove that this SDK update fixes it.

Only the SDK, its source guards, and the obsolete parser patch change. Reconnect timing, Bluetooth profiles, authentication and encryption settings remain the same. Diagnostics remain available for a direct comparison.

## Patch review

- 5.5.5 already checks SDP attribute capacity before writing and validates packet lengths. Keep its parser unchanged; retain the shared 16-attribute capacity required for Tesla's 10-attribute Device ID request. Update the baseline regression: upstream now accepts exactly eight attributes.
- The failed-feature-read callback is unchanged upstream. Retain the narrow closed-link guard which preserves the saved pairing on status 0x02.
- Review the HCI, L2CAP, SDP and HFP trace insertion points against the exact new files, preserving upstream checks and callback guards. All 13 replacement translation units retain strict source hashes and marker counts.
- Run captured Tesla discovery requests, capacity/overflow checks, bond-recovery callback tests, redaction/bounds tests, host protocol tests and a complete ESP32 build with SDK configuration checks.

## Hardware comparison

Install the separate Board B comparison image and pair with Tesla once, because full installation erases saved pairing. Enable Sync Messages and verify `test`. Then reset B and wait up to 90 seconds without pressing Connect in Tesla. Repeat with a five-second power disconnection. Finally test after iPhone has taken the Tesla connection; keep the iPhone phone key enabled. Save one full log covering any failed reconnection and a manual connection attempt. Compilation is not hardware validation.

Rollback is the unchanged main installer, Board B `0.3.1-alpha+d460f0158324`. Full rollback installation also erases pairing.
