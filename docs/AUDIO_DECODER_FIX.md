# HFP receive decoder correction

The September 22 hardware isolation run produced a clean generated tone on
Tesla, but robotic Tesla microphone loopback. The loopback bypasses inter-board
audio transport and resampling. Phone-side listening was inconclusive. This
narrows investigation without proving the cause of the audible problem.

Inspection of the pinned ESP-IDF v5.5.5 HFP AG and HF client receive paths found:

- Normal decoding and packet-loss concealment pass one-byte length variables
  through casts to the decoder's four-byte read/write length pointer.
- Two assembled 30-byte packets are passed with a length of 30 instead of 60.
- The SBC bit reader prefetches beyond the encoded frame. Passing an exact-size
  receive allocation or the 57-byte concealment frame lacks readable padding.

`firmware/audio_codec/patch.py` generates build-local copies of both codec
translation units, checks the original source hashes, uses real 32-bit lengths,
passes the assembled size, and copies each decode input into a bounded buffer
with zero-filled lookahead padding. It also checks the HCI header length and
caps the HF client's declared payload length to available bytes. The SDK checkout
is unchanged. CMake substitutes both source files and fails if either is absent.
Both board images must be rebuilt.

`IDF_PATH=/path/to/esp-idf python3 tools/test_audio_codec.py` compiles the actual
upstream receive and decode functions with the actual SBC decoder and PLC on the
host, preserving ESP32 integer widths. AddressSanitizer reproduces stack length
overreads in both original roles, on both normal and concealment paths. Corrected
code passes repeated complete and split frames, damaged-packet concealment,
checksum failure and recovery, truncated packets, and CVSD passthrough.

These are reproduced software defects. Whether correcting them resolves the
Tesla's robotic voice still requires repeating the microphone loopback and an
answered call in both directions. No HD-audio quality claim follows from a build
or host test alone.
