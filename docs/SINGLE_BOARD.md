# Single-board prototype

Firmware: **0.2.1-dev**. [Online installer](https://thomasgregg.github.io/DashBridge/).
The permanent two-board snapshot is [v0.1.4](https://github.com/thomasgregg/DashBridge/releases/tag/v0.1.4).

## What changed

One original ESP32-WROOM-32 (4 MB flash) runs both the iPhone ANCS receiver
and Tesla MAP/HFP services on the existing dual-mode Bluetooth stack.
Notifications travel through a bounded internal queue instead of UART2.
There are no jumper wires and no second board. USB provides power and the
setup console. Both Bluetooth transports advertise the name **DashBridge**.

The iPhone and Tesla have separate two-minute pairing windows. Pairing or
reconnecting one does not close the other's window. Existing bonds may
reconnect after the relevant window closes. `pair` opens both windows;
`pair phone` and `pair car` open only the requested one.

The local queue holds at most eight phone-to-car messages. A session reset
clears queued old-session messages even when the queue is full. Readiness
updates are coalesced. Callbacks enqueue work; the main task dispatches it,
so one Bluetooth callback does not recursively run the other endpoint.
WhatsApp filtering and duplicate handling are unchanged.

## Install and test while parked

This is an experimental build, not an established replacement for v0.1.4.
The earlier two-board B firmware displayed a test message on the user's
Tesla. That result does **not** establish that this combined build works.

### Initial 0.2.0-dev build verification

- [ESP-IDF v5.5.1 build and protocol tests](https://github.com/thomasgregg/DashBridge/actions/runs/35521656978) passed, including the Tesla service-discovery regression checks.
- The merged image is 1,153,904 bytes; the application has 29% of its flash partition remaining. This does not measure runtime RAM.
- Installer tests passed (8/8); desktop and 390 px mobile previews were checked with no horizontal overflow.

### Hardware report and notification fix — 20 September 2026

The user confirmed that the single-board test notification appeared on the Tesla.
The uploaded log then showed both ANCS and Tesla notification connections ready
at the same time, with 109,916 bytes of free internal RAM (minimum 106,908).
After a restart, both connections returned to ready; long-term reconnection
without a BLE setup app remains unverified.

A new WhatsApp notification appeared on the iPhone but not on the Tesla.
The firmware incorrectly interpreted ANCS flag `0x10` (NegativeAction, such as
Dismiss) as PreExisting. Apple's [ANCS flag table](https://developer.apple.com/library/archive/documentation/CoreBluetooth/Reference/AppleNotificationCenterServiceSpecification/Appendix/Appendix.html)
defines PreExisting as `0x04`. This can silently discard fresh notifications
with a dismissal action and allow old ones without that action through the gate.
The old log has no event flags, so it cannot prove which flags this particular
message carried.

Version 0.2.1-dev corrects that gate and adds a regression test for fresh
notifications with action flags and old notifications with and without actions.
It also logs event flags and forwarding/filter decisions without message text,
sender names or group names. The [0.2.1-dev build and regression checks](https://github.com/thomasgregg/DashBridge/actions/runs/35522683948) passed. The new regression also fails when the original incorrect flag is restored. The user subsequently confirmed that a real WhatsApp message appeared on the Tesla. The iPhone was paired directly through Settings, with no nRF Connect app.

### Priority-phone requirement

The next power-cycle test returned the Tesla to the iPhone as its active phone,
without reconnecting DashBridge. The iPhone must remain the priority device,
with normal calls and music. Making DashBridge the priority phone does not meet
that requirement. Version 0.2.1-dev advertises HFP and MAP and still competes
with the iPhone; successful notification delivery does not establish coexistence.
An [isolated message-only experiment](https://github.com/thomasgregg/DashBridge/blob/message-only/docs/MESSAGE_ONLY.md)
tests this separately, without replacing the regular installer.

### Setup steps

1. Save the v0.1.4 release or use its recovery guide before changing the board.
2. Connect one original ESP32 by USB. Install `single-merged.bin` at address
   `0x0`, or use the [online installer](https://thomasgregg.github.io/DashBridge/). The merged install erases
   saved pairings. RST is not a BOOT button; use the USB console commands below.
3. Open **Logs & Console** at 115200 baud. Confirm `App version: 0.2.1-dev`
   and `DashBridge single-board prototype` appear without repeated restarts.
4. Enter `pair car`. Forget the old **DashBridge B** pairing on the Tesla,
   then pair **DashBridge** and enable **Sync Messages**. Do not remove the
   iPhone phone key under Locks.
5. Wait for `Ready for new-message notifications`, then enter `test`.
   Confirm **DashBridge test** appears on the Tesla.
6. Enter `pair phone`. Connect the iPhone to the BLE **DashBridge** accessory.
   If it is absent from Bluetooth Settings, use a BLE setup utility such as
   nRF Connect for Mobile to scan and connect. Accept pairing and notification
   sharing. Enable **Share System Notifications** in Bluetooth details if
   offered. This setup helper does not sign into WhatsApp.
7. Wait for `iPhone link encrypted` and `ANCS ready`. Enter `status` and check
   that both iPhone and Tesla notifications report `ready`.
8. Lock the iPhone, then have someone send a **new** WhatsApp message. Confirm
   it appears on the Tesla and that no duplicate SMS/iMessage appears on the
   iPhone. Notification previews and Focus can limit the content iOS exposes.
9. Save the log. It reports free internal RAM, its low-water mark and the
   largest available block every 30 seconds and whenever `status` is entered.
   These measurements must be taken with both connections active.

Then test closing the setup utility, disconnecting/reconnecting each device
independently, power cycling, leaving and returning to the car, repeated
messages, group messages and emoji. Old notifications must not replay after
reconnection. Confirm the phone key still works.

Do not assume automatic iPhone reconnection works: it remains a physical test.
Calls, music passthrough and WhatsApp replies are still not implemented.

## USB commands

| Command | Action |
|---|---|
| `pair phone` | Open only iPhone pairing for two minutes |
| `pair car` | Open only Tesla pairing for two minutes |
| `pair` | Open both pairing windows |
| `test` | Queue a test notification once the Tesla message channel is ready |
| `status` | Report both notification connections, pairing windows and memory |
| `help` | List the commands |

## Build this version

Activate ESP-IDF v5.5.1, then run `bash tools/build.sh single`.
This explicitly builds only the single-board target. The default automatic build
on this branch also considers only `single`, so stale legacy A/B images do not
trigger unrelated builds. CI's manual **board**
selection also accepts `single`. The artifact is `dashbridge-firmware-single`.
Run `bash tools/test.sh` and `python3 tools/test_build_scope.py` for host tests.
After compiling, run `DASHBRIDGE_BUILD_ROLE=single python tools/test_sdp.py`
and `python tools/test_sdp_attributes.py` with the pinned SDK active.

The single-board installer is built with `npm ci --prefix web` then
`npm run build --prefix web`. Serve `_site` over localhost or HTTPS using
Chrome/Edge. GitHub Pages serves this experimental single-board installer.
The preserved v0.1.4 release includes the earlier two-board installer.

## Roll back

Use [the two-board recovery guide](https://github.com/thomasgregg/DashBridge/blob/v0.1.4/docs/RESTORE_TWO_BOARD.md).
For the board already tested with the Tesla, restore v0.1.4 `car-merged.bin`,
re-pair **DashBridge B**, and run `test` again. The release includes its own
archived web installer and does not depend on this development branch.
