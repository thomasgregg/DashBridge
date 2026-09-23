# DashBridge iPhone app

This is the native companion for the simplified setup flow. It finds Board A over Bluetooth, reads live setup status, saves the selected app IDs on Board A, and can reopen iPhone pairing. Tesla setup can be deferred. The test button schedules a local iPhone notification; Board A temporarily permits only that test through the normal ANCS path, without saving the companion app in the allowlist. Ordinary notification text and call audio continue to travel between the phone, boards, and Tesla; the app is not in that path.

## Run the preview

Open `DashBridge.xcodeproj` in Xcode and choose an iPhone simulator. The simulator cannot pair with the ESP32 boards, so tap **Preview without hardware** on the welcome screen to walk through the screens. On the app-choice page, iOS may ask once for App & Website Usage permission to show the installed-app list. Granting it in the simulator affects the simulator only. Before relying on that list, inspect the installed app's signed entitlements: an unsigned or ad-hoc command-line build may not contain the Family Controls entitlements, even if `App.entitlements` lists them. Such a build can preview other screens but cannot load installed apps.

The Xcode project is generated from `project.yml` with `xcodegen generate`. After changing the project spec, regenerate it before building.

Run simulator navigation checks on a dedicated test simulator, not the one used for manual preview: `xcodebuild -project DashBridge.xcodeproj -scheme DashBridge -destination 'platform=iOS Simulator,id=TEST_SIMULATOR_ID' test`. Xcode installs the test build over the app on its destination simulator, and an unsigned test build will break the real installed-app lookup there. The UI tests themselves use a debug-only preview and sample app list; they do not ask for App & Website Usage permission or touch real hardware.

## Test with a physical iPhone

The app requires iOS 26.4 or newer, Bluetooth permission, and a signed build with Apple's Family Controls and App & Website Usage entitlements. The installed-app list is available to customer installations only where Apple permits that API (EU device and EU Apple Account). Until those signing/eligibility requirements are met, the preview works but a physical-device install cannot be promised. Only apps returned by iOS appear as suggestions; the API is not a guaranteed inventory of every built-in system app, and it does not report which apps have notifications enabled. iMessage is part of Apple's Messages app and already works through Tesla's own phone connection.

For testing with the release images, install matching firmware on both boards. The app's Bluetooth setup service is on Board A; the local-notification test button requires Board A firmware with command 4 support. An application-partition-only update preserves stored Bluetooth pairings and app choices when the partition layout matches; a full image flash can erase them. Board B still handles the Tesla connection. In the car, test either with the new button or a fresh notification from an allowed app. The button asks for iPhone notification permission and then asks you to lock the phone so iOS can present the notification. Keep the boards linked and powered. Building this app never flashes either board automatically.

## What still needs a real-device check

The simulator has no ESP32 Bluetooth link, and the Tesla is not available in the simulator. On real hardware, verify first-time pairing and notification permission, automatic rediscovery after a power cycle, saved app choices after reboot, and whether iOS forwards the app's local test notification through ANCS to the Tesla. The app reports the board's status; it does not claim a Tesla message appeared until the user confirms it.
