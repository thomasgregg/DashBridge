# Board A notification diagnostics

Board A uses ESP-IDF **v5.5.5**. Its iPhone Bluetooth names are **Dash Calls**
for the Classic HFP connection and **Dash Messages** for BLE notification access.
No extra iPhone app is required in the observed setup.

## Discovery and pairing

The BLE advertisement solicits ANCS and includes the generic HID service UUID
`0x1812` and appearance `0x03c0`, following the pinned SDK's ANCS example. Its
scan response advertises the released setup-service UUID so Apple's
AccessorySetupKit picker can authorize BLE and bridge the Classic profiles.
Both packets fit within the 31-byte BLE limits. This is a discovery
advertisement, not a full keyboard implementation; no input reports are sent.

The iOS 27 test initially showed no notification connection in Settings.
After the discovery change and fresh pairing, the notification-sharing prompt
appeared. Logs confirmed encryption, ANCS discovery and both subscriptions.
The user subsequently confirmed text delivery through Board B to Tesla when
Tesla was connected. Calls and notification sharing also reconnected after
firmware updates. These results do not establish daily reliability.

## Capture a setup failure

1. Open Board A's USB console at 115200 baud. Use `status` without resetting it
   when preserving the current connection or failure evidence matters.
2. Send `pair phone`. In iPhone Settings → Bluetooth, pair **Dash Messages**
   first and accept the prompts.
3. Enable **Share System Notifications** for Dash Messages. Wait for the board
   to report notification readiness, then pair **Dash Calls** when it appears.
   Record the iOS version and firmware version with the log.
4. Check for both `Call profile: ready` and `iPhone notifications: ready`.
5. For delivery testing, ensure Tesla is connected to Dash Tesla and the car
   notification service is ready before receiving a new WhatsApp message.

## Interpret the log

- `BLE GATT registration complete` and `BLE privacy complete` identify setup callbacks.
- `BLE advertisement (ANCS + generic HID discovery)` and the scan-response log
  identify advertisement and name configuration.
- `BLE advertising start complete` with status zero means advertising started;
  it does not prove the iPhone saw it.
- `BLE connection event` shows the BLE connection reached the callback.
- Encryption, MTU, ANCS discovery and subscription logs show subsequent progress.
- `ANCS ready; waiting for car and new notifications` confirms notification setup,
  not successful delivery to Tesla.

The status command and periodic report include advertising state and setup flags.
Configuration status `-1` means no completion callback has been observed; `0`
means success. Other values are SDK errors. `discovery_started=1` means discovery
was attempted, not that it succeeded. These fields do not drive connection policy.
Notification diagnostic logs contain state, handles and error codes, not message
text or encryption keys.

Use the [current setup guide](SETUP.md) for wiring and firmware installation,
and the [README](../README.md#project-status) for current evidence and remaining
hardware tests.
