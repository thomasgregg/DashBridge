# DashBridge setup

Use the [two-board setup and wiring guide](CALL_RELAY.md) for current firmware.
It specifies **five wires**, including the audio link, and ESP-IDF **v5.5.5**.

The current firmware uses these Bluetooth names:

| Device | Name | Purpose |
| --- | --- | --- |
| Board A on iPhone | Dash Calls | Calls |
| Board A on iPhone | Dash Messages | Notification sharing |
| Board B on Tesla | Dash Tesla | Tesla phone/message connection |

Pair both A entries through iPhone Settings and enable Share System Notifications
for Dash Messages. No extra iPhone app was needed in the observed setup. On Tesla,
connect Dash Tesla and enable Sync Messages; normal iPhone-to-Tesla calls are
replaced by the bridge while it is selected as the active phone. The phone key
remains separate.

Install both **0.3.3-alpha** roles for the tone and loopback tests.
See the [release notes](releases/0.3.3-alpha.md) for image identities and limitations.

WhatsApp text delivery has worked. Call audio remains distorted, music forwarding
is not implemented, and daily reliability is not established. See the
[current validation record](CALL_RELAY_VALIDATION.md) and
[tone/loopback test instructions](AUDIO_ISOLATION_TESTS.md).

For updates to already provisioned boards, application-only flashing at `0x10000`
with the matching layout preserves saved pairings. Full merged-image installation
at `0x0` replaces the layout/storage and can erase them; do not interchange the
application and merged files.
