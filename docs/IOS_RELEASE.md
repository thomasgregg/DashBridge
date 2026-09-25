# DashBridge iOS app 1.0 (build 16)

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

The `ios-v1.0-b16` tag creates the independent GitHub release record after a
clean build. TestFlight or App Store distribution remains a signed Xcode/App
Store Connect step because signing credentials are not stored in this
repository.

## Build 16 changes

- Reworked Welcome around one concise power instruction and the shared setup
  artwork treatment.
- Added an explicit **Connect my iPhone** action before iOS pairing and
  notification-sharing prompts can appear.
- Combined the internal checking, notification-sharing, and Dash Calls stages
  into one continuous connection-progress screen without changing their
  required ordering.
- Added a one-time green-and-mint completion celebration using iOS's native
  particle emitter. It remains touch-transparent and does not replay when the
  Ready screen is revisited.
- Expanded reducer and UI coverage for the explicit connection action, unified
  progress screen, Welcome hierarchy, timeout behavior, and interactive
  completion celebration.
