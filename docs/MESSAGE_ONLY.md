# Message-only compatibility test

Firmware: **0.2.2-map-only**. Bluetooth name: **DashBridge Test**.

## Requirement

The iPhone stays the Tesla's priority phone, with its normal hands-free calls
and Bluetooth music connection. A WhatsApp notification on the dashboard is
not a successful result if reaching it displaces that connection.

## What this experiment changes

The previous v0.2.1-dev build delivered a real WhatsApp notification, as reported
by the user. It advertises a minimal HFP phone gateway as well as MAP messages.
That creates competition with the real iPhone for the Tesla's phone connection.

This isolated experiment removes both HFP roles from the Bluetooth build and
removes the HFP registration and call callbacks. Its Bluetooth device class is
uncategorized with object transfer, without telephony or audio bits. MAP over
RFCOMM/SPP and the iPhone BLE ANCS receiver remain, including the ANCS flag fix.
The SPP transport still advertises its own service; this is not a claim that
MAP is the only SDP record. No automatic reconnect attempts take over the phone.

Bluetooth defines messaging separately from calls. That does not establish
that Tesla supports it on a second accessory. The car might hide this device,
refuse pairing, omit Sync Messages or switch away from the iPhone anyway.
A discovery failure with this device class does not prove every MAP-only
arrangement is impossible; it rejects this particular candidate.

## Test while parked

1. Keep the iPhone selected as **Priority Device** in the Tesla Bluetooth menu.
2. Install the experiment from its separate online test page. It erases the
   ESP32's saved pairings. Forget only the old **DashBridge** entries on the
   Tesla and iPhone, to avoid cached service information. Keep the iPhone's
   Tesla pairing and phone key intact.
3. Open **Logs & Console**. Confirm `0.2.2-map-only` and the startup message
   `Message-only experiment: no HFP or audio service`.
4. Enter `pair car`. Search from the Tesla for **DashBridge Test** while the
   iPhone remains its connected phone. If absent, save the logs and stop.
5. If visible, try pairing. Do **not** mark DashBridge Test as the priority
   device. If the Tesla disconnects the iPhone, reconnect the iPhone, save the
   logs and stop: this candidate has not met the requirement.
6. If the Tesla keeps the iPhone connected and offers Sync Messages for
   DashBridge Test, enable it. Enter `status`, then `test` once Tesla
   notifications report ready. Record whether the test message appears.
7. Only if step 6 succeeds, enter `pair phone` and pair **DashBridge Test**
   directly in iPhone Settings > Bluetooth. Allow notification sharing.
   Once both notifications report ready, lock the iPhone and receive a new
   WhatsApp message. Confirm calls and Bluetooth music still use the iPhone.
8. Only after coexistence succeeds, repeat the USB power-cycle test without
   manually switching devices. Record the result and save logs.

Do not repeatedly re-pair or promote DashBridge if the Tesla rejects this mode.
The purpose is to test coexistence, not to force the previous phone behavior.

## Restore

Use the [regular installer](https://thomasgregg.github.io/DashBridge/) to restore
v0.2.1-dev. That version has demonstrated WhatsApp forwarding but still competes
with the iPhone as a phone device. Restoring it does not solve coexistence.
To return to normal calls and music, keep your iPhone connected and unplug
DashBridge; no firmware restore is required for that.

## Verification

Host protocol tests cover the unchanged message path. The build checks confirm
that HFP roles are disabled and HFP public APIs are absent from the linked image,
while the SDP, RFCOMM and BLE client entry points remain. Neither check proves
that Tesla will discover or accept the accessory; hardware results are pending.
