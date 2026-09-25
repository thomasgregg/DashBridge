# DashBridge sources and provenance

- **NotifyDrive discussion:** [Solving my messages notification issue with DIY](https://www.reddit.com/r/TeslaLounge/comments/1pw4lws/solving_my_messages_notification_issue_with_diy/). No public firmware repository is linked from the post.
- **Related discussion:** [Tesla forum topic](https://tff-forum.de/t/whatsapp-und-weiteres-im-tesla-empfangen/34001/298). No source-code link is included there.
- **Apple ANCS protocol:** [Apple Notification Center Service specification](https://developer.apple.com/library/archive/documentation/CoreBluetooth/Reference/AppleNotificationCenterServiceSpecification/Specification/Specification.html). Defines notification events, UIDs, attributes and pre-existing-event flags.
- **Espressif SDK:** [ESP-IDF v5.5.5](https://github.com/espressif/esp-idf/tree/v5.5.5), pinned commit `b774170ff46c393eeb5e495ea37936038d3f4f4f`.
- **Espressif's ANCS example:** [ble_ancs](https://github.com/espressif/esp-idf/tree/v5.5.1/examples/bluetooth/bluedroid/ble/ble_ancs). Consulted for the ESP-IDF connection/discovery API sequence. Our bounded queue and parser are separate code; this is not NotifyDrive source.
- **Bluetooth MAP:** [Bluetooth SIG Message Access Profile](https://www.bluetooth.com/specifications/specs/message-access-profile-1-4-3/). The prototype advertises a MAP 1.0 subset; it is not certified or fully conformant.
- **Tesla Bluetooth and phone key:** [Model 3 Bluetooth manual](https://www.tesla.com/ownersmanual/model3/en_us/GUID-3D90EA76-8DE3-4808-B7E4-1979EF299F3A.html). The phone key and the ordinary phone/media Bluetooth connection are separate. Verify the controls against the software installed in your own car.
- **Flashing tool:** [Espressif esptool-js](https://github.com/espressif/esptool-js).

The SDK has its own licenses, principally Apache-2.0 with component-specific terms. The SDK source checkout for these images retains the upstream licenses; copies of the SDK and controller root licenses are included under `third_party/`. This project is an independent local experiment and is not affiliated with Tesla, Apple, WhatsApp or NotifyDrive.
