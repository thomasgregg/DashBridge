# DashBridge iOS app 1.0 (build 25)

The companion app finds Board A, guides iPhone pairing, reads live setup
status, saves notification app choices, and can send a local test notification.
It is not in the call, music, or notification forwarding path after setup.

## Firmware compatibility

App 1.0 uses Setup GATT v1 and is compatible with firmware 0.5.4-alpha. App
and firmware releases are independent: the app can be updated without
rebuilding firmware while Setup GATT v1 remains supported. See the
[compatibility matrix](../contracts/COMPATIBILITY.md).

## Validation status

The app builds for the iOS simulator, its setup-state unit tests pass, and its
complete setup navigation suite passes. The app starts every fresh session on
Welcome and does not start Bluetooth until the user confirms **It's plugged in**.
The simulator cannot test ESP32 Bluetooth, Apple notification sharing, or a
Tesla.
A signed physical-device build also requires the Apple entitlements described
in the [iOS guide](../ios/README.md). First pairing, reconnection, notification
delivery, and the complete parked-car flow remain physical release checks.

The `ios-v1.0-b25` tag creates the independent GitHub release record after a
clean build. TestFlight or App Store distribution remains a signed Xcode/App
Store Connect step because signing credentials are not stored in this
repository.

## Build 25 changes

- Keeps ordinary Retry non-destructive: a powered-off board no longer loses a
  valid AccessorySetupKit authorization merely because it is temporarily absent.
- Adds a clearly explained **Set up connection again** path for a reset or
  replaced Board A. Authorization removal happens only after the user chooses
  it and Apple confirms it.
- Reconnects one exact saved accessory without claiming that an iPhone popup is
  expected, and never guesses between several authorized boards.
- Pauses setup deadlines behind Apple-owned prompts and ends a partial
  BLE/notification/calls wait after 30 seconds without forward progress.
- Adds unit and UI coverage for saved reconnection, explicit replacement,
  multiple boards, partial setup, and the recovery instructions.

## Build 24 changes

- Removes a stale AccessorySetupKit authorization before retrying Apple's
  picker, so an advertising board is not hidden as already authorized.
- Clears only the matching cached Bluetooth identifier during that recovery;
  firmware and saved notification-app choices are left untouched.

## Build 23 changes

- Combines BLE pairing, notification access, and Bluetooth Classic transport
  bridging into one user-facing DashBridge setup operation.
- Keeps one stable progress screen while Apple presents prompts and while the
  app performs one bounded automatic recovery attempt.
- Requires live BLE, notification, calls, and complete Tesla transport status
  before setup is reported as finished.
- Returns stale accessory authorization to Apple's picker instead of attaching
  to a different nearby board.

## Build 22 changes

- Removes the unnecessary Apple follow-up instruction screen after accessory
  authorization; normal setup stays inside DashBridge.
- Pauses connection deadlines while the app is inactive, so system prompts or
  a brief trip away from the app cannot cause a false interruption.
- Automatically reconnects an accessory that Apple already authorized when the
  app becomes active again.

## Build 21 changes

- Activates Apple's Bluetooth Classic transport bridging on the actual Core
  Bluetooth connection instead of only declaring support in the accessory
  picker.
- Applies transport bridging to first pairing and later reconnects, while still
  requesting notification sharing only when ANCS authorization is needed.
- Adds a regression test and release validation rule that fail if the app stops
  activating the Dash Calls transport.
