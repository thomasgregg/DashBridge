# Local checks and firmware builds

Use local tests before building firmware or asking for another in-car trial.

## What runs without hardware

`bash tools/test.sh` compiles the production protocol code and runs sanitizer checks for messages, UART, call control, audio buffering and reconnect policy. The reconnect replay additionally extracts the production peer-storage and reconnect functions and drives them with a virtual clock and the sanitized events in `tests/fixtures/tesla-reconnect-failure.json`.

The replay verifies persistence across a simulated reboot, rejection of missing/invalid bonds, timeout handling, the observed retry schedule and recovery after a later successful connection. NVS, Bluetooth API results and connection outcomes are substitutes supplied by the test. **It does not reproduce Tesla's radio handshake or explain the remote disconnect.** Its value is quickly detecting a regression in our handling of those events.

With the matching SDK installed, `bash tools/local_dev.sh check /absolute/path/to/esp-idf` also tests the actual SDK SDP parser, late feature failures and SSP confirmation callback. These tests do not need an ESP32, GitHub or a firmware installation.

## Native build setup

Keep the SDK and its tools in an ignored directory, for example `build/local-sdk/esp-idf` and `build/local-sdk/tools`. Use the exact SDK pinned by the checkout: v5.5.5 at `b774170ff46c393eeb5e495ea37936038d3f4f4f`. Use a supported Python version (this Mac has Python 3.12).

```sh
git clone --depth 1 --branch v5.5.5 --recursive --shallow-submodules --jobs 8 \
  https://github.com/espressif/esp-idf.git build/local-sdk/esp-idf
export IDF_TOOLS_PATH="$PWD/build/local-sdk/tools"
python3.12 build/local-sdk/esp-idf/tools/idf_tools.py install --targets esp32
python3.12 build/local-sdk/esp-idf/tools/idf_tools.py install cmake ninja
python3.12 build/local-sdk/esp-idf/tools/idf_tools.py install-python-env
```

Run from the matching firmware checkout:

```sh
bash tools/local_dev.sh check /absolute/path/to/esp-idf
bash tools/local_dev.sh build /absolute/path/to/esp-idf
```

The helper reuses `build/car` for incremental builds and also enables a local compiler cache when `ccache` is installed. It builds and packages only Board B, then verifies the generated Bluetooth configuration and SDP record. It does not flash hardware, publish an installer, or erase pairing. The package step updates the local `dist` files; those outputs must be reviewed before any release.

Keep the build directory between iterations. A no-change build should do very little work. A source change recompiles affected files; shared SDK/configuration changes can legitimately require a broader rebuild. Official release builds and final artifact verification remain useful, but do not need to be repeated for every hypothesis.

## Where hardware is still required

[Espressif's QEMU feature table](https://github.com/espressif/esp-toolchain-docs/blob/main/qemu/README.md#supported-features) lists Bluetooth as unsupported. A simulated `0x13` disconnect can exercise our response; it cannot establish why a real Tesla or controller generates it. Real interoperability, power-cycle reconnection and radio timing still need the board and car. Use one controlled hardware test only after the local checks establish the expected behavior of the proposed change.
