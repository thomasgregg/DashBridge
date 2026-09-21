# HD voice upgrade — 21 September 2026

> **Historical first HD build.** Later changes use **460800 baud**, fix buffer
> draining, and add isolation tests. Answered calls subsequently negotiated mSBC
> on both boards but sounded bad. `empty_reads` now counts normal empty SDK probes
> and is not, by itself, proof of an audible underrun. Use the
> [current validation record](CALL_RELAY_VALIDATION.md) and
> [test guide](AUDIO_ISOLATION_TESTS.md); the values and pending checks below
> describe this earlier build.

The previous firmware explicitly disabled wideband speech on both boards and
rejected mSBC connections. This build enables the stock ESP-IDF v5.5.5 mSBC
negotiation and internal PCM codecs on both HFP roles. Each local audio-state
event selects 16 kHz mSBC or 8 kHz CVSD independently.

The wired audio transport now carries 240 bytes of signed 16-bit little-endian
mono PCM per 7.5 ms at a fixed 16 kHz rate. Its 256-byte CRC-protected frame takes
about 2.78 ms on the existing full-duplex 921600-baud UART; no wiring change is
required. A 31-tap fixed-point halfband filter converts the CVSD fallback to or
from the wire rate. The mSBC path passes decoded PCM unchanged. Buffer timing
remains a 15 ms initial prefill and at most 60 ms queued audio.

The call-control identifier is `calls/2` and the audio frame version is `2`.
Message forwarding uses a separate control link. Application-only installation
at `0x10000` preserves NVS and Bluetooth bonds with the configured flash layout.

## Automated validation

- Host suite with AddressSanitizer and UndefinedBehaviorSanitizer: notification
  filtering/transport, call controls, framing/CRC/resynchronization, sequence and
  bounded-buffer behavior, saved-peer persistence and reconnect replay.
- Audio packets recover after corruption.
- Filter tests: streaming chunk independence, 1 kHz gain preservation,
  interpolation-image suppression, greater than 40 dB rejection of a 6 kHz tone
  before 8 kHz decimation, 3 kHz speech-band preservation, saturation and reset.
- Production callback replay: all four combinations of 8/16 kHz endpoints,
  including fragmented input, exact output duration, pitch and level, bit-exact
  16-to-16 kHz transport, stale-audio rejection and disconnected callbacks.
- Generated configurations require wideband speech on both boards, internal
  codecs, HCI audio, correct HFP roles, and the unchanged passive Tesla gateway.

These checks do not establish Bluetooth negotiation, microphone quality, echo,
independent Bluetooth clock drift, or long-call stability on physical hardware.

## Hardware validation still required

The user heard FaceTime ringing through the Tesla on the previous firmware but
had **not answered**. That confirms call signaling/ringing, not relayed speech.
The Tesla may generate that ringtone locally, so enabling HD voice may not
change the ringtone itself.

1. After updating both boards, let existing pairings reconnect.
2. Receive and answer a FaceTime Audio or WhatsApp voice call while parked.
3. Verify intelligible speech in both directions and hang-up from Tesla.
4. Check both consoles for `Local call audio: mSBC HD; 16000 Hz`. If one side
   reports `CVSD fallback; 8000 Hz`, that side still limits end-to-end bandwidth;
   the rate converter preserves timing but cannot recreate missing detail.
5. During a sustained call inspect received/dropped/underrun counter changes.
   Startup priming can increment counters; sustained increases need diagnosis.
6. Confirm a fresh WhatsApp notification after the call and after a power cycle.

This remains a one-call alpha. It adds no music, video, Siri, conference, contact,
message-reply, acoustic echo-cancellation, or custom noise-reduction support.

## Installation record

Board A application `0.3.2-alpha+5f825f824863` was flashed at `0x10000`
with hardware-MAC gating and flash hash verification. Startup was verified:
iPhone HFP became ready at about 8 seconds and ANCS at about 14 seconds.
Existing pairings survived. At 20 seconds, free internal RAM was 101,720 bytes.

Board B application `0.3.2-alpha+db4f6e373608` was then flashed with the same
hardware identity and hash checks. Startup verified call protocol v2 and the
other board ready. Its saved Tesla bond remains present. At 20 seconds, the
Tesla HFP and MAP connections were not yet ready; free internal RAM was 135,636
bytes. The Tesla must reconnect before the answered-call test. HD negotiation
and answered-call quality remain untested.
