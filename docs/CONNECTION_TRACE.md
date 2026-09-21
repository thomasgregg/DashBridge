# Reading a reconnect capture

> **Historical vendor-stack trace build.** These traces describe the earlier outgoing-reconnect investigation. Current firmware uses the stock ESP-IDF v5.5.5 Bluetooth component and passive Tesla reconnection; it does not emit all these patched trace markers. See [current diagnostics](BLUETOOTH_AUDIO_DIAGNOSTICS.md) and [validation](CALL_RELAY_VALIDATION.md).

Use the full downloaded console log, not just the final status block. The
firmware version identifies the exact build. `Connection trace v1` marks the
combined controller/channel/service instrumentation. No additional debug flags
or capture hardware are needed for these host-side traces.

## Timeline

1. `Starting outgoing HFP reconnect attempt` identifies a local attempt.
2. `HCI TX op=0x0405` requests the physical Classic connection.
3. HCI connection-complete status and the existing ACL/bond log show its result.
4. Remote feature/version and role-change events describe link negotiation.
5. Link-key request/reply metadata, authentication completion and encryption
   change show whether the existing bond was accepted. Key bytes are omitted.
6. L2CAP queue/controller/RX records correlate signaling by handle and command ID.
7. SDP connect/configure and request/response traces identify service discovery.
8. HFP ready and MAP ready identify usable services; a queued test alone is not
   proof of delivery. Confirm message retrieval/acknowledgment and the car screen.

`HCI TX` is a request, `HCI STATUS` is its controller acceptance/rejection, and
`HCI RX` includes later results. `L2CAP TX-queued` means queued by the host;
`TX-controller` means handed to the HCI transport, not over-the-air acknowledgment.
A link handle may be reused after a disconnect; split each capture into link
lifetimes. HFP role 1 is the outgoing attempt; role 0 is the incoming connection.
Do not infer direction from a handle's value.

## Wire fields

Hex fields use Bluetooth wire byte order (multi-byte values little-endian).
`bytes` is the declared parameter size; `captured` is the emitted prefix length.
Secrets and unlisted command parameters are omitted, not indicated by zero values.

| Record | Data fields |
| --- | --- |
| HCI op `0405` | address (6), packet type (2), scan repetition (1), reserved (1), clock offset (2), role-switch permission (1) |
| HCI op `0406` | handle (2), locally requested reason (1) |
| HCI event `03` | status (1), handle (2), address (6), link type (1), encryption enabled (1) |
| HCI event `05` | status (1), handle (2), disconnect reason (1) |
| HCI event `06` | authentication status (1), handle (2) |
| HCI event `08` | status (1), handle (2), encryption enabled (1) |
| HCI event `12` | status (1), address (6), resulting role (1) |
| HCI event `23` | status (1), handle (2), page (1), max page (1), features (8) |
| HCI event `0e` | command credits (1), opcode (2), status (1); key-size query additionally includes handle (2), size (1) |
| L2CAP code `01` | reject reason (2), optional reason-dependent fields |
| L2CAP code `02` | PSM (2), source CID (2) |
| L2CAP code `03` | destination CID (2), source CID (2), result (2), status (2) |
| L2CAP code `04` | destination CID (2), flags (2), configuration options |
| L2CAP code `05` | source CID (2), flags (2), result (2), configuration options |
| L2CAP code `06` / `07` | destination CID (2), source CID (2) |
| L2CAP code `0a` / `0b` | information type (2); response adds result (2), then type-specific fields |

Channel state and event numbers refer to the pinned ESP-IDF v5.5.1 `l2c_int.h`.
`SDP_CONN_FAILED = 0xfff1` is a generic failed connection, not proof of a missing
service. Reason `0x13` reports remote user termination but does not explain the
remote device's policy. A successful service query on an already-connected link
does not establish that a fresh outgoing link can be established.
