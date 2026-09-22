# Audio isolation tests

These diagnostic modes are temporary and off by default. They do not claim to
fix the reported robotic audio. Both firmware roles include the commands and
remote control support; either USB console can control both boards.

First establish an answered call routed through Dash Calls and Dash Tesla.
Keep both boards powered and their control wiring connected. No command places,
answers, or ends a call, and a test cannot be armed for a future call.

| USB command | Expected sound | Audio path tested |
| --- | --- | --- |
| `audio car tone` | Tesla plays a steady quiet 1 kHz tone | B generator → B Bluetooth encoder/radio → Tesla |
| `audio phone tone` | Remote caller hears the same tone | A generator → A Bluetooth encoder/radio → iPhone → call network |
| `audio car loopback` | Speak into Tesla; hear your voice returned through Tesla | Tesla microphone → B Bluetooth decoder → B encoder → Tesla speakers |
| `audio phone loopback` | Remote caller speaks and hears their voice returned | Call network/iPhone → A Bluetooth decoder → A encoder → iPhone/call network |
| `audio off` | Normal two-way call audio resumes | Both boards return to the normal relay |
| `status` | No audio change | Show local/peer mode, time remaining, audio and radio counters |

Run one test at a time, with `audio off` between tests. Confirm the peer's
`Other board audio test: ... applied` response for remote commands. A timeout
means the command is not confirmed; inspect status before judging the sound.
The opposite caller may hear silence during an isolation test. Use low speaker
volume for loopback; its returned samples are attenuated by four (about 12 dB)
to reduce acoustic feedback. Prefer a caller elsewhere using headphones.

Each activation lasts at most 60 seconds and ends on local audio disconnection,
codec/session change or reboot. Mode and buffered test audio are not persisted.
`audio off` clears queued audio before restoring the relay. Remote commands are
bound to the current peer boot and audio session and deduplicated, so repeats
cannot extend a test or activate it on a later call.

The tone is mono signed 16-bit PCM at the negotiated 8 or 16 kHz rate. Peak
amplitude is 2048 (about -24 dBFS), frequency 1 kHz, frames 7.5 ms. Generation is
deadline paced; the callback drains a bounded queue and then returns zero.
`test_late` records missed generation deadlines; `test_overrun` records queue
pressure/discontinuities. A bad tone with these counters increasing does not
establish a Bluetooth fault. Software tests verify timing, frequency, amplitude,
loopback sample values, bounded backlog, stale-data rejection and normal recovery.

The selected board's test audio bypasses the inter-board audio UART and rate
conversion. The other board, BLE, control tasks, radio environment, and call
network are still present. These tests isolate audio paths, not every possible
hardware/software cause. Use the same call's direct iPhone audio as a baseline.
Clean tests on both sides with a bad normal relay point toward the bridge path;
a bad result on one side narrows further investigation without proving a cause.

Validation: both ESP-IDF v5.5.5 role builds, sanitizer tests, production audio
callback replay at both rates, and production diagnostic-control replay for both
roles passed. Hardware listening on September 22 found a clean Tesla tone and robotic Tesla microphone loopback; phone-side listening was inconclusive.

Both applications are now installed and startup verified. A restored iPhone
calls and notifications; B was awaiting Tesla reconnection at the latest check.
The remote `audio off` acknowledgement was verified from B to A. The September 22 listening result and subsequent receive-decoder correction are documented in [AUDIO_DECODER_FIX.md](AUDIO_DECODER_FIX.md). Capture `status` before `audio off` to retain the test counters
in the log, because starting a new mode resets them. See [installed versions](CALL_RELAY_VALIDATION.md).
