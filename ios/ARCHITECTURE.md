# DashBridge iOS architecture

This document defines the architecture of the DashBridge iPhone app. Setup is
presented as one DashBridge connection even though Board A exposes a BLE setup
identity and Bluetooth Classic profiles. The app must preserve that user model
and the conditions that trigger iOS-owned permission and pairing dialogs.

## Design goals

1. Keep one authoritative owner for setup navigation and saved setup progress.
2. Keep Core Bluetooth callbacks out of screen-routing decisions.
3. Treat Setup GATT v1 as a released wire contract independent of firmware
   implementation details.
4. Make every setup transition testable without an ESP32, while retaining a
   physical-device release gate for Apple-owned system dialogs.
5. Keep one stable phone-setup screen while Apple-owned prompts and automatic
   recovery temporarily interrupt the app.

## Component diagram

```mermaid
flowchart LR
    User[User actions] --> Views[Existing SwiftUI screens]
    Views -->|SetupFlowEvent| Flow[SetupFlowStore and pure SetupFlowMachine]
    Flow -->|SetupFlowState| Views
    Flow -->|SetupFlowEffect| Shell[SetupView effect runner]

    Shell --> BLE[BridgeBluetooth adapter]
    BLE --> ASK[AccessorySetupKit picker and transport bridging]
    Shell --> Apps[AppCatalog adapter]
    Shell --> Notify[UserNotifications adapter]

    BLE --> Contract[Typed Setup GATT v1 contract]
    Contract --> BoardA[Board A firmware]

    Flow --> Progress[SetupProgressPersisting]
    BLE --> Peripheral[RememberedPeripheralPersisting]

    Tests[Unit and UI tests] -.-> Flow
    Tests -.-> Contract
```

Dependency direction points away from the screens and toward small interfaces
or pure value types. `SetupFlowMachine` does not import SwiftUI or Core
Bluetooth. `BridgeContract` knows the released UUIDs, status layout, and command
encoding, but not scanning, pairing, timers, or navigation.

## State and effect model

`SetupFlowState` is the single owner of:

- the current screen;
- saved setup progress;
- back-navigation context;
- whether the car screen is being reviewed; and
- whether the explanatory connection screen still needs to trigger a
  connection request; and
- the phase of the single phone-setup operation.

The view sends a `SetupFlowEvent`. The pure reducer updates state and returns
zero or more `SetupFlowEffect` values. The thin effect runner performs platform
work such as starting Bluetooth, connecting a discovered peripheral, retrying,
or loading the app catalog. Platform callbacks return as new events instead of
mutating navigation directly.

```mermaid
stateDiagram-v2
    [*] --> Welcome
    Welcome --> Finding: Confirm DashBridge is powered
    Finding --> PhoneSetup: accessory session ready
    PhoneSetup --> PhoneSetup: picker / pairing / bridging / verification / bounded recovery
    PhoneSetup --> Apps: BLE, notification sharing, and calls ready
    Apps --> Car: choices saved
    Car --> Test: Dash Tesla and calls connected
    Car --> Ready: Do this later
    Test --> Ready: user confirms message
    Ready --> Apps: Change apps
    Ready --> Car: Finish in the car
    PhoneSetup --> Help: terminal adapter failure
    PhoneSetup --> Welcome: Back
```

This diagram describes the primary path. The reducer tests also cover returning
users, an already-connected peripheral, saved deferral, review navigation,
device replacement, retries, and cached status arriving before a screen change.

`PhoneSetupPhase` records presentation detail such as awaiting the user's tap,
Apple's system setup, verification, and recovery. Those phases never become
separate navigation owners. `BridgeBluetooth` owns one bounded automatic
recovery attempt and reports typed events; only the reducer decides whether a
terminal failure navigates to help. Automatic recovery accepts only the
peripheral identifier authorized by AccessorySetupKit. If that identity is
stale or unavailable, retry removes that stale app authorization and returns
to Apple's picker instead of attaching to
another nearby board.

## System-dialog contract

iOS owns the visual presentation and final scheduling of its system dialogs.
The app controls only the operation that may cause a dialog. These trigger
points must not move during refactors.

| System behavior | Application trigger | Required ordering |
| --- | --- | --- |
| Bluetooth application permission | First creation of `CBCentralManager` | Every fresh app session first shows Welcome. Setup and Bluetooth begin only after the user confirms **It's plugged in**. |
| Unified DashBridge phone setup | AccessorySetupKit selection with BLE pairing and Classic transport bridging, followed by `connect(_:options:)` with `CBConnectPeripheralOptionRequiresANCS` and `CBConnectPeripheralOptionEnableTransportBridgingKey` | The app first shows connection guidance without requesting a connection. After the user taps **Connect my iPhone**, it transitions to **Connecting your iPhone** and immediately asks iOS to present the picker. If the accessory session is still activating, the request is retained and presented as soon as activation completes. iOS may show pairing and notification prompts at different times; the app remains on the same progress screen until Setup GATT proves BLE, notifications, and calls are ready. |
| Encrypted policy access | Read the policy characteristic | Only after status reports notification sharing ready, avoiding a competing security exchange. |
| App & Website Usage | Load the installed-app catalog | When entering app selection or the ready screen, never during launch. |
| Local notification permission | Request authorization from `UNUserNotificationCenter` | Only after the user taps **Send test notification**. |

Simulator tests verify the app's trigger ordering. They cannot prove whether an
iOS dialog appears. First-pair, denied-permission, previously-paired, and
reconnection cases therefore remain physical-iPhone release checks.

## Setup GATT boundary

`BridgeContract.swift` mirrors `contracts/setup_gatt_v1/contract.json`:

- three fixed characteristic UUIDs;
- an exact three-byte, version-1 status value;
- typed status-bit decoding; and
- typed commands for allow, deny, phone pairing, and notification testing.

Raw operation numbers must not appear in views or Bluetooth lifecycle code.
Any contract version change must update the JSON contract, the typed codec,
firmware validation, compatibility documentation, and contract tests together.

## Persistence

`SetupProgressPersisting` owns the existing user-default keys and stores
onboarding, app-choice completion, Tesla deferral, test confirmation, and the
last completed peripheral identifier. Replacing the physical board keeps the
legacy onboarding marker but resets board-specific completion decisions.

The historical `dashbridge.welcomed` key is retained for compatibility with
installed versions, but it no longer skips Welcome on a fresh app session.

`RememberedPeripheralPersisting` separately owns the Core Bluetooth peripheral
identifier used for reconnection. Keeping this separate prevents transport
caching from becoming setup-flow state.

The existing keys are intentionally unchanged, so the refactor does not reset
installed users.

## Testing and release gates

### Fast automated checks

- `BridgeContractTests` freezes status decoding, exact frame length, contract
  version, and command encoding.
- `SetupFlowTests` exercise navigation and side effects as pure state
  transitions.
- `SetupFlowUITests` retain screen, action placement, back behavior, deferral,
  diagnostics, and preview regression coverage.

### Physical-device matrix

Run these before an iOS release that changes setup, Bluetooth, permissions, or
timing:

1. New installation with Bluetooth permission undecided.
2. Bluetooth denied, then enabled in Settings and retried.
3. First unified AccessorySetupKit setup: BLE pairing, Classic bridging, and
   notification-sharing approval.
4. Notification-sharing denial and subsequent recovery.
5. Classic calls profile becomes ready without opening Bluetooth Settings.
6. Existing bonds after app relaunch and bridge power cycle.
7. Remembered peripheral unavailable, followed by discovery of the live board.
8. Replacement Board A resets board-specific setup progress.
9. Tesla setup deferred, then completed later.
10. Local notification permission allowed and denied from the test screen.

Record the screen visible before each system dialog, the resulting screen, and
the status bits reported by Board A. Simulator success does not replace this
matrix.

## Rules for future changes

- Add new navigation behavior as a state, event, reducer rule, and unit test.
- Add platform work as an effect or adapter operation, not inside a screen.
- Do not route from a Core Bluetooth delegate callback; emit an event instead.
- Do not add another persistence owner for setup progress.
- Do not duplicate Setup GATT UUIDs, bits, or operation numbers.
- Keep user-facing error copy outside the protocol decoder.
- Preserve the single-setup user model and system-dialog trigger ordering unless
  a product change explicitly authorizes it and updates the compatibility tests
  and this document.
