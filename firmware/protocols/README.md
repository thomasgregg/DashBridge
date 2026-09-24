# Protocols

Versioned DashLink schemas and pure codecs live here. Protocol messages are
typed by domain. A generic message whose interpretation depends on unrelated
global state is not permitted.

`obex` is the shared, state-free codec for packet framing, headers and
application parameters. Bluetooth profiles such as MAP and PBAP remain in
adapters; profile UUIDs and domain behavior do not belong in the OBEX codec.

`calls_v3` contains the released call-state and dedicated call-audio codecs.
It consumes portable call value types but has no dependency on Bluetooth,
ESP-IDF, the legacy runtime, or the generic inter-board envelope.

`dashlink_v2` is the sole board-to-board control codec. Its packet variant has
separate types for heartbeats, notifications, call control, music state and
commands, and contact snapshot transfer. It is bounded, checksummed, rejects
invalid domain states, and resynchronizes after corrupt input. Real-time call
and music audio deliberately remain outside DashLink on the dedicated media
transport.
