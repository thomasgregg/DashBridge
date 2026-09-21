# Minimal Tesla outgoing HFP reconnect reproduction

This is Espressif's stock ESP-IDF v5.5.5 `hfp_ag` example with one isolated
addition: `main/reconnect.c` remembers one bonded HFP peer and attempts an
outgoing service-level connection after boot. It contains no DashBridge MAP,
SPP, custom SDP, inter-board UART, call relay, or retry implementation.
It also logs the stock GAP ACL completion events so the controller's remote
disconnect reason is visible without an HCI sniffer.

The two hooks added to the stock example report HFP profile readiness and HFP
connection-state callbacks to the reconnect module. If the module has no saved
peer and exactly one Classic Bluetooth bond exists, it adopts that bond. A
successful manual HFP service-level connection also saves its peer. Retries use
5, 10, 20, 40, then 60 second delays and a 20 second per-attempt timeout.

## Build

Use the pinned ESP-IDF v5.5.5 checkout:

```sh
./build-local.sh /absolute/path/to/esp-idf
```

## Flash without erasing the Tesla bond

Do not erase flash and do not flash a merged image. From an activated ESP-IDF
environment, write the three generated images so the NVS partition containing
the existing Bluetooth bond is preserved:

```sh
idf.py -B build -p PORT flash monitor
```

The helper performs the same non-erasing flash and rejects the wrong ESP-IDF
revision:

```sh
./flash-local.sh PORT /absolute/path/to/esp-idf
```

The partition layout matches DashBridge's single-app-large layout. On first
boot, look for `Adopted the only existing bonded peer`, followed five seconds
later by `Outgoing HFP reconnect attempt 1`. The serial log reports `ACL
connect complete ... status/reason=0xNN` and `ACL disconnected ...
status/reason=0xNN` for comparison with the full DashBridge firmware. A connect
status of `0x00` is success; `0x04` is a page timeout.

If there is no existing bond, connect manually from Tesla once. When the HFP
service-level connection succeeds, the log prints `SLC ready; peer saved=ESP_OK`.
Then reset or remove power for five seconds and save the complete serial log.

The decisive comparison is:

1. automatic outgoing reconnect after reset or power restoration; and
2. a manual Tesla **Connect** action without resetting the ESP32 between them.

If the automatic attempt again produces an ACL connection followed by remote
reason `0x13`, this stock example reproduces the failure independently of
DashBridge's MAP, SPP, UART, and relay code.

## Reading the result

- Automatic SLC success here points back to a DashBridge interaction or timing
  issue.
- Automatic ACL connect followed by remote reason `0x13`, while Tesla's manual
  **Connect** succeeds on the same boot, isolates the problem to the
  ESP-IDF/Tesla outgoing connection direction rather than DashBridge's extra
  profiles.
- No ACL connection at all points to paging, peer selection, or lost bond data.
- A manual connection that also fails means the run is not comparable to the
  original reconnect failure and the bond should be established again first.

## Hardware result — 21 September 2026

The image was installed without writing the ESP32's NVS partition. At boot it
loaded the existing Tesla bond. While the car was unavailable, the first outgoing attempt
ended with HCI connection-complete status `0x04` (page timeout). After the user
entered and woke the Tesla, the Tesla UI showed `ESP_HFP_AG_RECONNECT`
automatically reconnected; the user did not press **Connect**.

The UI success was reported while the serial capture was being stopped, so this
run did not establish which endpoint initiated the successful connection. It
showed only that the stock HFP profile, existing bond, ESP32, and Tesla could
form a working connection. Later uninterrupted full-profile captures resolved
the connection direction as described below.

## Final direction and priority result

With the full HFP + MAP
image, each ESP-initiated `esp_hf_ag_slc_connect()` attempt opened an ACL and
Tesla closed it about 150–160 ms later with HCI reason `0x13` (remote user
termination). This was unchanged by Classic-only controller mode, an untouched
ESP-IDF Bluetooth component, stable scan mode, or the stock HFP feature mask.

When **Connect** was selected in Tesla, the reverse direction succeeded on the
same firmware and bond: HFP reached `SLC_CONNECTED`, the MAP transport opened,
Tesla issued MAP requests, and the MNS transport reported `Ready for new-message
notifications`. This proved the library, bond, HFP implementation, MAP record,
and message transport were functional.

Board B now stays connectable and listens instead of initiating an HFP SLC.
After the paired `DashBridge B` device was enabled as Tesla's **Priority Device**
for the active driver profile, Tesla initiated HFP + MAP automatically. A
subsequent ESP32 hard reset also recovered without selecting **Connect**; the
firmware reported both `Call profile: ready` and `Tesla notifications: ready`.
The practical reconnect contract is therefore Tesla-owned connection initiation
plus the vehicle's Priority Device setting, with Board B remaining a passive HFP
gateway.
