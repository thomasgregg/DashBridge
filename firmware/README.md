# Firmware layout

- `apps`: Board A and Board B composition roots only.
- `core`: portable domain logic and the ports it defines.
- `protocols`: stable, versioned wire formats and codecs.
- `adapters`: Bluetooth profiles and other external-system adapters.
- `transport`: DashLink channels, scheduling, retry, and backpressure.
- `platform`: ESP-IDF-specific runtime services.

Architecture checks enforce these boundaries so feature work cannot introduce
a second state owner, broad wrapper, or generic transport.
