# ADR 0002: Separate typed control and real-time media links

- Status: accepted
- Date: 2026-09-24

## Context

DashBridge relays small state changes and commands as well as continuous call
and music audio. These traffic classes have conflicting failure and scheduling
requirements: control must be bounded, validated, ordered, and recoverable;
audio must arrive continuously with low latency and may discard late frames.

The former generic board message reused notification fields for unrelated
domains. That hid invalid combinations, coupled feature changes, and made
control routing dependent on global interpretation.

## Decision

UART2 is the sole application-control link and uses DashLink v2. Its packet
variant has distinct types for heartbeat, notification, call, music, and
contact traffic. Every payload has domain-specific validation, size bounds,
versioning, and a checksum. Call control is prioritized within the control
queue.

UART1 is the real-time media link and carries only dedicated call-audio and
music-audio codecs. Music metadata and AVRCP commands are control traffic;
actual SBC frames are media traffic. Calls continue to own audio focus and
preempt music.

The released iOS setup GATT v1 contract is independent of both internal links
and remains unchanged.

## Rejected alternatives

- One universal message with optional fields recreates the invalid-state and
  cross-domain coupling problems of the removed generic envelope.
- Sending audio through the reliable control queue allows bulk frames to delay
  setup, calls, notifications, contacts, and heartbeats.
- Independent per-feature physical links add wiring, buffering, and lifecycle
  complexity without improving the two scheduling classes that actually exist.

## Consequences

- Both boards must run matching DashLink v2 firmware; an old and new image are
  deliberately incompatible and fail closed at the frame magic/version.
- New control features require a typed packet and explicit dispatch branch.
- Audio loss does not corrupt control state, and large contact/message traffic
  cannot directly stall the media stream.
- Architecture checks reject a return of the generic envelope or media frames
  inside DashLink.
