# Adapters

Adapters translate ANCS, HFP, A2DP, AVRCP, MAP, PBAP, setup GATT v1, NVS, and
other external APIs into core ports and typed events. They may depend on the
core's port interfaces; the core may not depend on adapters.

The car MAP adapter projects the portable message store into Tesla's MAP and
MNS profiles. It depends on the message core and the generic OBEX protocol but
does not own message state or depend on ESP-IDF/runtime globals.

The calls ESP HFP adapter owns the external Bluetooth boundary for both roles:
Board A is an HFP client toward the iPhone and Board B is an HFP audio gateway
toward the Tesla. Its SCO/eSCO audio transport is contained in the same adapter,
while call state remains owned by the portable calls controller.
The adapter's remaining runtime effects are an explicit migration seam. Call
control already crosses the boards as a typed DashLink call message.

The music adapter maps the iPhone A2DP sink and AVRCP controller to the Tesla
A2DP source and AVRCP target. Portable state owns metadata sessions, replay
protection, and audio focus; a call always preempts music without changing the
user's play/pause intent, allowing the Bluetooth streams to resume naturally.
