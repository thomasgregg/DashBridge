# DashBridge iOS app 1.0 (build 14)

The companion app finds Board A, guides iPhone pairing, reads live setup
status, saves notification app choices, and can send a local test notification.
It is not in the call, music, or notification forwarding path after setup.

## Firmware compatibility

App 1.0 uses Setup GATT v1 and is compatible with firmware 0.5.3-alpha. App
and firmware releases are independent: the app can be updated without
rebuilding firmware while Setup GATT v1 remains supported. See the
[compatibility matrix](../contracts/COMPATIBILITY.md).

## Validation status

The app builds for the iOS simulator, its setup-state unit tests pass, and its
complete setup navigation suite passes. The app now starts every fresh session
on Welcome and does not start Bluetooth until the user taps **Get started**.
The simulator cannot test ESP32 Bluetooth, Apple notification sharing, or a
Tesla.
A signed physical-device build also requires the Apple entitlements described
in the [iOS guide](../ios/README.md). First pairing, reconnection, notification
delivery, and the complete parked-car flow remain physical release checks.

The `ios-v1.0-b14` tag creates the independent GitHub release record after a
clean build. TestFlight or App Store distribution remains a signed Xcode/App
Store Connect step because signing credentials are not stored in this
repository.

## Build 14 changes

- Added a clear power-on prerequisite below the Welcome illustration, before
  **Get started** begins Bluetooth discovery.
- Centralized setup navigation, back behavior, timeouts, and progress in a
  tested state machine without changing the existing screen flow.
- Added typed Setup GATT v1 status and command boundaries.
- Preserved the timing of Bluetooth, ANCS, App & Website Usage, and local
  notification permission triggers.
- Fixed launches skipping Welcome because of the historical persisted
  onboarding marker.
- Added architecture diagrams, system-dialog documentation, unit tests, and
  expanded release gates.
