# DashBridge iOS app 1.0 (build 19)

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
complete setup navigation suite passes. The app starts every fresh session on
Welcome and does not start Bluetooth until the user confirms **It's plugged in**.
The simulator cannot test ESP32 Bluetooth, Apple notification sharing, or a
Tesla.
A signed physical-device build also requires the Apple entitlements described
in the [iOS guide](../ios/README.md). First pairing, reconnection, notification
delivery, and the complete parked-car flow remain physical release checks.

The `ios-v1.0-b19` tag creates the independent GitHub release record after a
clean build. TestFlight or App Store distribution remains a signed Xcode/App
Store Connect step because signing credentials are not stored in this
repository.

## Build 19 changes

- Opens Apple's accessory picker immediately and only after **Connect my
  iPhone** is tapped; an early request waits for session activation instead of
  being dropped and appearing later.
- Always offers normal DashBridge discovery. A saved Core Bluetooth identifier
  is now an additional migration fallback rather than a migration-only flow.
- Returns to the connection guidance when the accessory picker is canceled, so
  setup can be retried without reopening the app.
- Keeps the secure BLE, notification-sharing, and bridged Calls/Music ordering
  introduced in build 17.
