# Architecture migration ledger

The target architecture is defined in `ARCHITECTURE.md`. This ledger records
which boundaries are physically enforced and which feature implementations are
still legacy-backed.

| Area | Boundary | Authoritative state | Legacy route removed | Status |
|---|---|---|---|---|
| Repository and CI | Enforced | n/a | n/a | Complete |
| iOS setup GATT v1 contract | Enforced | n/a | n/a | Complete |
| Setup (GATT, policy, pairing, status) | Enforced | Portable `setup::Controller` | Yes | Complete |
| Messages and history | Core state, Tesla MAP adapter, and typed DashLink notification messages enforced | Portable `messages::Store` on both boards | Legacy `Inbox`, MAP/MNS implementation, and generic wire route removed | Complete |
| Calls | Core state/replay policy, portable codecs, ESP-IDF HFP boundary, and typed DashLink call control enforced | Portable `calls::Controller` on both roles | Legacy state globals, protocol header, main-runtime HFP implementation, and generic wire route removed | Complete |
| Music and audio focus | Core state/focus, portable SBC framing, ESP-IDF A2DP/AVRCP boundary, and typed DashLink metadata/control enforced | Portable `music::Controller` on both roles | Legacy state, protocol, replay globals, main-runtime Bluetooth implementation, and generic wire route removed | Complete |
| Contacts and call history | Core store/session transfer, vCard codec, iPhone PBAP client, Tesla PBAP adapter, and typed DashLink transfer enforced | Portable `contacts::Store` with staged atomic replacement on Board B | Legacy phonebook state, vCard parser, PBAP server, main-runtime contacts implementation, and generic wire route removed | Complete |
| DashLink v2 | Typed, versioned, checksummed and bounded control protocol enforced | Per-domain portable state | Generic `WireMessage` codec and operation switch removed | Complete |
| Supervisor and composition roots | Board app, typed transport, injected runtime ports, and ESP platform components enforced | Portable domain controllers | `firmware/main` product source and broad runtime facade removed | Complete |

Setup's platform effects remain narrow outbound adapters: time, synchronization,
NVS persistence, recent app-name lookup, and the test-notification trigger. They
cannot mutate setup state directly. The released iOS GATT bytes and installed
NVS policy format are covered by compatibility tests.

Board A now owns the canonical relayed-message state and Board B owns a bounded
projection using the same portable state machine. Session resets, live adds,
silent history, updates, removals, read state, capacity and privacy bounds no
longer live in the MAP or generic-wire implementation. Generic OBEX framing and
header parsing now live in a portable protocol codec. Tesla MAP/MNS behavior
lives in a car-side adapter that consumes the Board B message projection and
owns no domain state. PBAP shares the OBEX codec without depending on MAP.
Notification changes now cross the boards only as typed DashLink messages.

Call snapshots, peer boot/sequence acceptance, command replay gates, phone
number policy and command authorization now live in the portable call core.
The calls/3 state codec and call-audio framing live under protocols. ESP-IDF HFP
client and gateway callbacks now live in the calls adapter, outside the legacy
runtime. Call control uses named DashLink call fields; it no longer borrows a
notification payload or depends on a generic operation code.

Music metadata, session/revision acceptance, AVRCP command replay protection,
metadata bounds, and call-over-music audio focus now live in the portable music
controller. SBC framing lives in `protocols/music_v1`; iPhone A2DP sink/AVRCP
controller and Tesla A2DP source/AVRCP target behavior live in the music adapter.
During calls, new music is rejected and queued pre-call frames are drained so
they cannot play after focus returns. Music metadata and AVRCP commands use
typed DashLink control messages. SBC audio remains on the dedicated media link.

Contacts, favorites, addresses, and incoming/outgoing/missed/combined call
history now share a bounded portable store. Board B stages each ordered snapshot
and replaces the last good directory only after a valid completion, so stale
sessions, gaps, malformed entries, interrupted syncs, and failed retries cannot
publish partial data or erase the last usable snapshot. The bounded vCard parser
lives in `protocols/contacts_v1`; iPhone PBAP client behavior lives in the ESP
contacts adapter; Tesla phonebook/listing/single-vCard rendering lives in the
car PBAP adapter over the shared OBEX codec. Reset, entry, completion and
acknowledgement are distinct DashLink types, preserving atomic snapshot rules
without overloading message fields.

DashLink v2 runs on the reliable control UART and covers heartbeats,
notifications, call control, music metadata/control, and contact transfer.
Call and music audio remain on a separate real-time media UART using their
dedicated codecs. This is an intentional latency boundary, not a second control
architecture. Composition now lives in `apps/dashbridge_app`; typed queueing and
single-board deferred delivery live in `transport/dashlink_transport`; UART,
GPIO, timers, locking, NVS, Bluetooth-controller startup, logging and reset live
in `platform/esp_runtime`. Bluetooth adapters use an injected, narrow services
port and never depend on the board app. Architecture checks prevent a generic
envelope, reintroduction of the obsolete `firmware/main` or `bridge_core`
compatibility layers, or real-time audio entering the control transport.
