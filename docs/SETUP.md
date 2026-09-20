# DashBridge setup: first parked-car test

## Identify the parts

- Two **original ESP32-WROOM-32** development boards, such as the AZDelivery DevKit C V2 (ASIN B074RGW2VQ).
- A USB **data** cable for programming each board and USB power for both during use.
- Three female-to-female jumper wires.

Label the boards **A — iPhone** and **B — Tesla**. Leave them disconnected from each other for the initial car test.

## Load the firmware

The prebuilt merged images are `dist/car-merged.bin` for B and `dist/phone-merged.bin` for A. Each includes the bootloader, partition table and application. They are for a 4 MB original ESP32, not a different ESP32 family. Flashing a merged image also removes previous pairing stored on that board.

**[Open the DashBridge web installer →](https://thomasgregg.github.io/DashBridge/)** in **Chrome or Edge on a computer** (not iPhone, iPad or Safari).

1. Connect only the board being programmed with a USB **data** cable. Close any serial monitor first.
2. Select **A — iPhone** or **B — Tesla**, then click the install button.
3. Select that board’s USB serial port, then choose **Install** and confirm. The page selects the firmware and flash address automatically.
4. Keep the board connected until installation finishes. Close the installer dialog, unplug the board and label it **A** or **B** before connecting the other one.

If connection fails, hold BOOT while starting installation and release it once the chip is detected or installation begins. Press EN/reset after programming if the board does not restart. Start the parked-car test below with **B alone**, before wiring the two boards together.

For manual browser flashing, Espressif’s [browser flashing tool](https://espressif.github.io/esptool-js/) also accepts the merged files at **address `0x0`**. Use 115200 baud if a faster speed is unreliable.

Alternatively, from the project folder, with Python and `esptool==4.12.0` installed:

```sh
python3 tools/flash.py --list
python3 tools/flash.py --board car --port /dev/cu.YOUR_BOARD_PORT
```

Use `--board phone` for A. The script checks the local image checksum before flashing; it never chooses a serial port automatically. Flash only the purchased development board.

## Test 1: Tesla, with Board B alone

1. Power Board B. On a new board, pairing is open for two minutes. To reopen it later, hold **BOOT for two seconds**, then release. EN restarts the board; BOOT performs the test action only after firmware has started.
2. While parked, use the Tesla Bluetooth controls to add **DashBridge B** as a phone. Accept the pairing prompt and enable **Sync Messages** if that option appears.
3. Wait several seconds, then briefly press and release BOOT on B.
4. Look for a notification from **DashBridge test** with “Your Tesla received a test notification from DashBridge.”

If the board pairs but the car offers no message synchronization, or the test message never appears, stop at this test. That is a compatibility result we need to investigate. It does not mean the real WhatsApp connection is already working. Do not change or delete the phone key under Tesla's Locks settings.

The serial output is 115200 baud. Useful milestones are `Tesla message transport connected` and `Ready for new-message notifications`. Logs contain status codes, not message bodies. Capture these logs if the car refuses the service.

**Calls are not available through this prototype.** After the test, select the real iPhone as the car's active phone again. Leave its Tesla phone key intact.

## Test 2: iPhone notification access

1. Load `phone-merged.bin` onto A and power it. New-board pairing opens for two minutes; BOOT for two seconds reopens it.
2. Connect to **DashBridge A**. Generic BLE accessories may not appear in iPhone Settings. If it is absent, a BLE utility such as Nordic's **nRF Connect for Mobile** can scan for and connect to it. This utility is a prototype setup aid; automatic connection without it is not yet established.
3. Accept the iPhone pairing and notification-sharing prompts. In the accessory's Bluetooth details, enable **Share System Notifications** if offered. Make sure WhatsApp notifications are allowed. Preview settings and Focus can affect what iOS exposes.
4. In the serial output, look for `iPhone link encrypted` followed by `ANCS ready; waiting for car and new notifications`.

Do not assume it will reconnect after the utility app closes, the iPhone locks, or the boards lose power. Those are explicit tests below. If access fails, reopen pairing, reconnect, and record the status log. There is no need to share WhatsApp credentials or scan a WhatsApp linked-device QR code.

## Connect the boards

Disconnect power before wiring. Use the GPIO numbers printed on the boards:

| Board A | Board B |
|---|---|
| GPIO **17** (TX2) | GPIO **16** (RX2) |
| GPIO **16** (RX2) | GPIO **17** (TX2) |
| **GND** | **GND** |

Power both boards using USB. **Do not connect their 5V/VIN/3V3 pins together.** The jumper wires carry 3.3 V signals and a shared ground, not power. The USB-to-computer serial connection uses other pins and remains available for logs.

## Test 3: a real WhatsApp notification

1. Power both boards and establish both Bluetooth connections.
2. With the car ready and the iPhone not showing the relevant conversation, have someone send a new WhatsApp message.
3. Check that the Tesla displays the sender and message, and that the phone has its normal WhatsApp notification without an extra SMS or iMessage.
4. Send another message, a group message, an emoji and a longer message. Check whether iOS groups them and whether this produces the expected car alerts.
5. Repeat with the phone locked and the setup utility closed, then after power cycling both boards. Confirm there is no replay of old notifications.
6. With the car stationary, check that the iPhone still unlocks the car as a phone key. Restore the iPhone as active Bluetooth phone and verify normal call routing before using the car normally.

This is a compatibility test, not a road-use release. Reliable unattended reconnection and calls/audio passthrough remain future work until these results are known.

## Reset or remove the prototype

Unplug the boards and select your iPhone in the Tesla Bluetooth controls. Remove the two bridge Bluetooth pairings if desired. To clear a board's own stored pairing, hold BOOT for at least eight seconds **after startup**, then release it. This resets that board only; it does not remove your Tesla phone key or change WhatsApp.
