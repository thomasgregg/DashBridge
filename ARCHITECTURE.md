# DashBridge architecture

## Source of truth

This repository is the only active DashBridge source tree. Product versions
live in `version.txt`; released source states live in Git tags. Directory names
must not be used as version identifiers.

Board A owns canonical phone-side state. Board B projects that state into the
Bluetooth profiles exposed to the Tesla. A typed and versioned DashLink
protocol is the only application-level communication between the boards.

## Dependency direction

```text
firmware/apps
     |
     v
firmware/core ---> firmware/core/ports
                         ^
                         |
     firmware/adapters, firmware/transport, firmware/platform
```

The core is portable C++ and contains domain state machines for setup,
messages, calls, music, contacts, connectivity, audio focus, projection, and
supervision. It must not include ESP-IDF, FreeRTOS, Bluetooth-stack, adapter,
platform, or legacy-runtime headers.

Board applications are composition roots. They construct the required
adapters and connect them to the core; they contain no feature decisions.

## Compatibility boundaries

- `contracts/setup_gatt_v1` freezes the released iOS setup interface.
- `firmware/protocols` owns stable wire schemas and codecs.
- `firmware/adapters` owns Bluetooth profile and external-system behavior.
- `firmware/transport` moves typed DashLink traffic with explicit priorities.
- `firmware/platform` owns ESP-IDF primitives such as tasks, timers, UART, and
  NVS.
- `firmware/apps/dashbridge_app` is the only product composition root.

The project uses ESP-IDF's normal component discovery, so no compatibility
`firmware/main` component is present. `app_main()` lives directly in the board
application component.

Adapters receive platform and app effects through the narrow portable
`runtime_ports` interface. They may not include or depend on the app.

## Migration completion

A domain is migrated only when its authoritative state lives in the new core,
both board applications use the new ports, compatibility tests pass, and the
corresponding legacy route has been deleted. The migration is complete; the
checks reject any return of the obsolete `firmware/main`, `legacy_runtime`, or
`bridge_core` compatibility layers and the broad runtime facade.
