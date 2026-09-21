# Audio buffer investigation — 21 September 2026

> **Investigation record.** The startup checks and pending test below were followed by the post-fix call result at the end of this file. This build has since been superseded; see [current status](CALL_RELAY_VALIDATION.md) and [isolation tests](AUDIO_ISOLATION_TESTS.md).

The user reports robotic/distorted answered FaceTime audio and no audible
microphone at the far end. An earlier “speech sounds clear” reply was explicitly
corrected and must not be treated as a successful voice test.

On the first HD build, a live Board B capture confirmed 16 kHz mSBC during a call.
The cumulative received/dropped/old-underrun counters went from 2264/215/2253 to
5832/569/5816 across the test. The old underrun counter is misleading: the pinned
SDK calls the outgoing PCM callback repeatedly until it returns zero for each
data-ready event. Empty reads are therefore expected even on a healthy stream.
The combined dropped counter did not distinguish transmit queue overflow,
receive sequence gaps, or playback overflow.

## Reproduced defect and correction

The PCM buffer cleared its primed state whenever that normal empty probe arrived.
It then waited for two new packets again, producing repeated two-packet bursts
instead of continuing with each new packet. The CVSD output resampler also reset
its filter on those probes, introducing repeated filter-startup transients.

A production-callback regression test reproduces the SDK drain-until-empty loop.
It fails on the previous code and passes with the fix at both 8 and 16 kHz.
The fix retains buffer priming, partial samples, and filter state on normal empty
probes. Explicit disconnects, audio-token/codec changes, sequence gaps, buffer
overflow, and the 60 ms stale-data timeout still clear the relevant state.
This is a confirmed software defect; its contribution to the user's audible
problem still needs a fresh hardware test.

## Added diagnostics

Both boards now report Bluetooth input/output byte counts, input peak amplitude,
wire transmit/receive frame counts, and separate transmit-full, transmit-stale,
receive-gap and playback-overflow counts. No speech samples or message text are
logged. The previous `underruns` label is now `empty_reads`.

Each board also shares a compact diagnostic snapshot over the existing control
wire every five seconds. Either USB console can show the other board's audio
counters once both updates are installed. This is an optional extension of call
protocol v2; the first HD firmware ignores it safely.

During a test, Board A input is the remote caller and Board B input is Tesla's
microphone. Compare counter deltas across boards, accounting for any 8/16 kHz
conversion, to locate loss. A nonzero input byte count with zero peak can mean
silence or a muted source; frame counts alone do not establish intelligible audio.

## Validation

Both ESP32 builds and generated codec/role checks pass. The sanitizer host suite
passes, including continuous callback output under SDK empty probing, bit-exact
16 kHz forwarding, all four endpoint sample-rate combinations, anti-alias filtering,
stream reset behavior, notification handling, and reconnect replay.

Physical call quality, microphone delivery and sustained operation are not yet
verified for this build. Existing pairings are preserved by app-only flashing.

## Installation

Both application images were installed with hardware-MAC checks and flash hash
verification, preserving pairing storage:

- Board A: `0.3.2-alpha+54f3ac023727`; iPhone HFP and ANCS automatically ready.
- Board B: `0.3.2-alpha+6bed81f7ea60`; protocol v2 peer detection verified.

Board A's console successfully displays both local and Board B audio counters.
At the startup check, Tesla had not reconnected. A fresh answered-call test is
pending; the software fix must not yet be described as resolving the audible
distortion or missing microphone.

## First post-fix call result

The user reported unclear microphone audio at the beginning and overall quality
still below HD. Both boards reported mSBC at 16 kHz. Tesla microphone input was
nonzero (peak 9666). After the call counters settled, A transmitted 3130 frames
and B received 2742; B transmitted 3121 and A received 2607. Receive sequence-gap
counters were A 436 and B 324, with zero transmit queue-full, stale-transmit or
playback-overflow counts. Some endpoint loss is expected at call teardown, but
this scale of loss persists throughout the call. The remaining fault is not
explained by a narrowband fallback or a completely absent microphone stream.

Next build tests lower UART baud, earlier FIFO servicing and shorter interrupt
masking, with explicit UART/CRC error counters. The buffering correction remains
valid, but it did not establish satisfactory hardware audio.
