# Contributing to DashBridge

DashBridge is an early hardware prototype. Useful evidence matters more than a broad feature list: compile success, a host test, and an observed result on a car are three different things.

## Start here

- Read the [README](README.md) for the scope and current limits.
- Follow the [setup guide](docs/SETUP.md) for hardware work.
- Read the [architecture guide](ARCHITECTURE.md) before changing Bluetooth or message handling.

## Hardware reports

Use the [hardware report form](https://github.com/thomasgregg/DashBridge/issues/new?template=hardware-report.yml). Include:

1. Board model and chip, firmware commit or image checksum, and power arrangement.
2. iPhone model and iOS version, if relevant.
3. Tesla model, model year, and software version.
4. The test stage, exact steps, expected result, and observed result.
5. Relevant serial status output at 115200 baud.

Start with Board B alone. A negative result, such as pairing working but message synchronization being unavailable, is useful. Remove personal information from logs before posting. Never include pairing keys, account credentials, or private message contents.

## Code changes

1. Fork the repository and create a branch for one focused change.
2. Activate ESP-IDF v5.5.5. Use the original `esp32` target.
3. Run `bash tools/test.sh` for protocol/core changes.
4. Run `bash tools/build.sh` for firmware changes. By default this rebuilds only stale roles and refreshes their packaged images and manifest; use `bash tools/build.sh both` to force both.
5. Update relevant documentation if behavior or setup changes.
6. Open a pull request explaining the problem, resulting behavior, and validation performed. State explicitly whether hardware was tested.

Host tests require Clang with AddressSanitizer and UndefinedBehaviorSanitizer. The project includes a `.clang-format` configuration. Generated build directories are ignored; do not commit them or the SDK checkout.

Keep notification queues bounded, avoid logging message content, and preserve clear failure behavior. Unsupported sends must not appear successful. Prefer tests of externally observable protocol behavior over tests that repeat an implementation's internal steps.

## Scope

The current firmware includes messages, calls, music, contacts, app policy, and
the two-board transport described in the architecture guide. Several paths still
need physical iPhone/Tesla validation, especially call audio, music controls,
contact sync, and reconnection. Other chips and phone platforms remain out of
scope unless they receive a separately defined design and hardware validation.
See the current status in the README.

Contributions to the original DashBridge project are provided under its [MIT License](LICENSE). Preserve the licenses and attribution of any third-party code you introduce.
