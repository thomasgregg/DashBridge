# Third-party notices

The MIT license at the repository root covers original DashBridge material. It does not replace licenses for ESP-IDF, its components, or code and libraries incorporated into firmware images.

The prebuilt prototype uses [ESP-IDF v5.5.1](https://github.com/espressif/esp-idf/tree/v5.5.1), revision `fcae32885b0296b32044cb99ecbdc50d98dddb83`.

- [ESP-IDF root license](ESP-IDF-LICENSE): Apache-2.0.
- [ESP32 controller root license](ESP32-controller-LICENSE): copied from the controller checkout used for the build.
- [Complete upstream component tree](https://github.com/espressif/esp-idf/tree/v5.5.1/components): component-level notices and licenses remain applicable.

The copies in this directory are root notices, not an exhaustive software bill of materials. Protocol references and the ANCS example consulted during development are listed in [sources and provenance](../docs/SOURCES.md).

The separate [Board B SDK comparison](https://thomasgregg.github.io/DashBridge/idf-5.5.5/) uses [ESP-IDF v5.5.5](https://github.com/espressif/esp-idf/tree/b774170ff46c393eeb5e495ea37936038d3f4f4f), including ESP32 controller revision [`b4b7c54b1eab6a844789b69e56322ef5736eddc8`](https://github.com/espressif/esp32-bt-lib/tree/b4b7c54b1eab6a844789b69e56322ef5736eddc8). Both root license files were compared against those checkouts and are unchanged. Component-level notices for [that SDK tree](https://github.com/espressif/esp-idf/tree/v5.5.5/components) apply to this image.
