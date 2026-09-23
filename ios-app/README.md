# DashBridge iPhone app

This is the native companion for the simplified setup flow. It finds Board A over Bluetooth, reads live setup status, saves the selected app IDs on Board A, can reopen pairing, and can ask Board B to send a test message. Notification text and call audio continue to travel between the phone, boards, and Tesla; the app is not in that path.

## Run the preview

Open `DashBridge.xcodeproj` in Xcode and choose an iPhone simulator. The simulator cannot pair with the ESP32 boards, so tap **Preview without hardware** on the welcome screen to walk through the screens. On the app-choice page, iOS may ask once for App & Website Usage permission to show the installed-app list. Granting it in the simulator affects the simulator only. For command-line simulator builds, keep ad-hoc signing enabled so the test entitlements are included; an unsigned build cannot read the app list.

The Xcode project is generated from `project.yml` with `xcodegen generate`. After changing the project spec, regenerate it before building.

## Test with a physical iPhone

The app requires iOS 26.4 or newer, Bluetooth permission, and a signed build with Apple's Family Controls and App & Website Usage entitlements. The installed-app list is available to customer installations only where Apple permits that API (EU device and EU Apple Account). Until those signing/eligibility requirements are met, the preview works but a physical-device install cannot be promised. Only apps returned by iOS appear as suggestions; the API is not a guaranteed inventory of every built-in system app, and it does not report which apps have notifications enabled. iMessage is part of Apple's Messages app and already works through Tesla's own phone connection.

Install the companion-app firmware on Board A before testing a physical iPhone. An application-partition-only update preserves stored Bluetooth pairings and app choices; a full image flash can erase them. The app's Bluetooth setup service is on Board A. Board B still handles the Tesla connection, but the current 0.4.2 Board B image does not understand the app's remote pairing and test commands yet. Those controls must not be presented as working until both boards use a compatible protocol. Keep the boards linked and powered. Building this app never flashes either board automatically.

## What still needs a real-device check

The simulator has no ESP32 Bluetooth link, and the Tesla is not available in the simulator. On real hardware, verify first-time pairing and notification permission, automatic rediscovery after a power cycle, saved app choices after reboot, and the Tesla test message. The app reports the board's status; it does not claim a Tesla message appeared until the user confirms it.
