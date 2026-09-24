# Firmware layout

- `apps`: Board A and Board B composition roots only.
- `core`: portable domain logic and the ports it defines.
- `protocols`: stable, versioned wire formats and codecs.
- `adapters`: Bluetooth profiles and compatibility adapters.
- `transport`: DashLink channels, scheduling, retry, and backpressure.
- `platform`: ESP-IDF-specific runtime services.

Architecture checks enforce these boundaries so feature work cannot drift back
into a shared supervisor, compatibility wrapper, or generic transport.
