#!/usr/bin/env python3
"""Enforce repository boundaries and released compatibility contracts."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path
from uuid import UUID


ROOT = Path(__file__).resolve().parent.parent
failures: list[str] = []


def require(condition: bool, message: str) -> None:
    if not condition:
        failures.append(message)


required_directories = (
    "contracts/setup_gatt_v1",
    "firmware/apps",
    "firmware/core",
    "firmware/adapters",
    "firmware/protocols",
    "firmware/transport",
    "firmware/platform",
    "ios",
)
for name in required_directories:
    require((ROOT / name).is_dir(), f"required architecture directory is missing: {name}")

require((ROOT / "firmware/protocols/dashlink_v2").is_dir(),
        "typed DashLink v2 protocol component is missing")
for path in (ROOT / "firmware").rglob("*"):
    if path.suffix in {".c", ".cc", ".cpp", ".h", ".hpp"}:
        require("WireMessage" not in path.read_text(errors="replace"),
                f"generic WireMessage returned in {path.relative_to(ROOT)}")
require(not (ROOT / "firmware/adapters/music/esp_a2dp_avrcp_adapter/music_wire_compat.hpp").exists(),
        "the generic music wire compatibility codec must not return")

require((ROOT / "version.txt").is_file(), "version.txt must be at the repository root")
require(not (ROOT / "firmware/version.txt").exists(),
        "firmware/version.txt must not return; version.txt is the only product-version source")

require(not (ROOT / "firmware/main").exists(),
        "firmware/main is obsolete; app_main belongs only in firmware/apps/dashbridge_app")
require(not (ROOT / "firmware/legacy_runtime").exists(),
        "the completed legacy-runtime marker directory must not return")
require(not (ROOT / "firmware/components/bridge_core").exists(),
        "the bridge_core compatibility wrapper must not return")
for obsolete_name in ("runtime.hpp", "bridge_core.hpp"):
    require(not any(path.name == obsolete_name for path in (ROOT / "firmware").rglob("*")),
            f"obsolete compatibility header must not return: {obsolete_name}")

forbidden_core_includes = (
    "esp_",
    "freertos/",
    "driver/",
    "hal/",
    "nvs",
    "sdkconfig.h",
    "bridge_core.hpp",
    "runtime.hpp",
    "legacy_runtime/",
    "adapters/",
    "platform/",
)
include_pattern = re.compile(r"^\s*#\s*include\s*[<\"]([^>\"]+)[>\"]", re.MULTILINE)
for portable_root in (ROOT / "firmware/core", ROOT / "firmware/protocols"):
    for path in portable_root.rglob("*"):
        if path.suffix not in {".c", ".cc", ".cpp", ".h", ".hpp"}:
            continue
        for include in include_pattern.findall(path.read_text(errors="replace")):
            if include.startswith(forbidden_core_includes):
                failures.append(
                    f"portable code {path.relative_to(ROOT)} imports forbidden dependency {include}"
                )
            if "protocols/obex" in path.as_posix() and include.startswith("dashbridge/core/"):
                failures.append(
                    f"protocol codec {path.relative_to(ROOT)} must not depend on domain core {include}"
                )

contract_path = ROOT / "contracts/setup_gatt_v1/contract.json"
try:
    contract = json.loads(contract_path.read_text())
except (OSError, json.JSONDecodeError) as error:
    failures.append(f"cannot read setup GATT v1 contract: {error}")
    contract = None

if contract:
    swift_path = ROOT / "ios/App/BridgeBluetooth.swift"
    setup_composition_path = ROOT / "firmware/apps/dashbridge_app/setup_composition.cpp"
    protocol_header_path = ROOT / "firmware/protocols/setup_gatt_v1/include/dashbridge/protocols/setup_gatt_v1.hpp"
    protocol_source_path = ROOT / "firmware/protocols/setup_gatt_v1/setup_gatt_v1.cpp"
    adapter_path = ROOT / "firmware/adapters/phone/setup_gatt_v1_adapter/setup_gatt_v1_adapter.cpp"
    swift = swift_path.read_text()
    setup_composition = setup_composition_path.read_text()
    app_main = (ROOT / "firmware/apps/dashbridge_app/app_main.cpp").read_text()
    phone_source = (ROOT / "firmware/adapters/phone/esp_ancs_adapter/esp_ancs_adapter.cpp").read_text()
    ancs_header = (ROOT / "firmware/adapters/phone/esp_ancs_adapter/include/dashbridge/adapters/ancs_codec.hpp").read_text()
    ancs_source = (ROOT / "firmware/adapters/phone/esp_ancs_adapter/ancs_codec.cpp").read_text()
    transport_header = (ROOT / "firmware/transport/dashlink_transport/include/dashbridge/transport/dashlink_transport.hpp").read_text()
    transport_source = (ROOT / "firmware/transport/dashlink_transport/dashlink_transport.cpp").read_text()
    platform_source = (ROOT / "firmware/platform/esp_runtime/esp_runtime.cpp").read_text()
    car_source = (ROOT / "firmware/adapters/car/esp_tesla_adapter/esp_tesla_adapter.cpp").read_text()
    obex_header = (ROOT / "firmware/protocols/obex/include/dashbridge/protocols/obex.hpp").read_text()
    obex_source = (ROOT / "firmware/protocols/obex/obex.cpp").read_text()
    map_header = (ROOT / "firmware/adapters/car/map_adapter/include/dashbridge/adapters/map_adapter.hpp").read_text()
    map_source = (ROOT / "firmware/adapters/car/map_adapter/map_adapter.cpp").read_text()
    calls_header = (ROOT / "firmware/core/dashbridge_domain_core/include/dashbridge/core/calls.hpp").read_text()
    calls_source = (ROOT / "firmware/core/dashbridge_domain_core/calls.cpp").read_text()
    calls_protocol = (ROOT / "firmware/protocols/calls_v3/include/dashbridge/protocols/calls_v3.hpp").read_text()
    hfp_adapter_path = ROOT / "firmware/adapters/calls/esp_hfp_adapter/esp_hfp_adapter.cpp"
    call_relay = hfp_adapter_path.read_text()
    music_header = (ROOT / "firmware/core/dashbridge_domain_core/include/dashbridge/core/music.hpp").read_text()
    music_source = (ROOT / "firmware/core/dashbridge_domain_core/music.cpp").read_text()
    music_protocol = (ROOT / "firmware/protocols/music_v1/include/dashbridge/protocols/music_v1.hpp").read_text()
    music_adapter = (ROOT / "firmware/adapters/music/esp_a2dp_avrcp_adapter/esp_a2dp_avrcp_adapter.cpp").read_text()
    contacts_header = (ROOT / "firmware/core/dashbridge_domain_core/include/dashbridge/core/contacts.hpp").read_text()
    contacts_source = (ROOT / "firmware/core/dashbridge_domain_core/contacts.cpp").read_text()
    contacts_protocol = (ROOT / "firmware/protocols/contacts_v1/include/dashbridge/protocols/contacts_v1.hpp").read_text()
    contacts_adapter = (ROOT / "firmware/adapters/contacts/esp_contacts_adapter/esp_contacts_adapter.cpp").read_text()
    pbap_header = (ROOT / "firmware/adapters/car/pbap_adapter/include/dashbridge/adapters/pbap_adapter.hpp").read_text()
    pbap_source = (ROOT / "firmware/adapters/car/pbap_adapter/pbap_adapter.cpp").read_text()
    call_audio_transport = (ROOT / "firmware/adapters/calls/esp_hfp_adapter/call_audio_external.cpp").read_text()
    dashlink_header = (ROOT / "firmware/protocols/dashlink_v2/include/dashbridge/protocols/dashlink_v2.hpp").read_text()
    dashlink_source = (ROOT / "firmware/protocols/dashlink_v2/dashlink_v2.cpp").read_text()
    protocol_header = protocol_header_path.read_text()
    protocol_source = protocol_source_path.read_text()
    adapter = adapter_path.read_text()
    all_swift = "\n".join(path.read_text() for path in (ROOT / "ios").rglob("*.swift"))

    require("esp_" not in setup_composition and "freertos/" not in setup_composition,
            "setup composition must not contain ESP-IDF or Bluetooth adapter code")
    require("LegacySetupPort" not in setup_composition,
            "the deleted legacy setup facade must not return")
    require("PairingWindows" not in app_main + transport_header + transport_source,
            "pairing-window authority must remain in the portable setup controller")
    require("static AppPolicy policy" not in phone_source,
            "allow-list authority must remain in the portable setup controller")
    require("class NotificationResponse" in ancs_header and
            "NotificationResponse::feed" in ancs_source and
            "core::messages::Message" in ancs_header,
            "ANCS response parsing must stay with the phone adapter and use portable message data")
    require("messages::Store message_state" in phone_source and
            "messages::Store inbox" in car_source,
            "both boards must use the portable message state machine")
    require("class Framer" in obex_header and "Framer::feed" in obex_source,
            "generic OBEX framing must remain an independent protocol codec")
    require("class Server" in map_header and "messages::Store &inbox_" in map_header and
            "MAP-msg-listing" in map_source and "MAP-event-report" in map_source,
            "Tesla MAP/MNS projection must remain in the car adapter")
    require("runtime.hpp" not in map_header + map_source and "esp_" not in map_header + map_source,
            "MAP adapter must depend only on portable core and protocol code")
    require("map_adapter::Server mas" in car_source and "obex::Framer framer" in car_source,
            "Board B must compose the MAP adapter with the shared OBEX protocol")
    require("class Controller" in calls_header and "Controller::apply_remote_snapshot" in calls_source and
            "CommandGate" in calls_header,
            "call state, peer sessions, and replay gates must remain in the portable call core")
    require("calls::Controller call_state" in call_relay and
            "static calls::State self" not in call_relay and
            "static calls::CommandGate commands" not in call_relay,
            "both HFP roles must use the portable call controller")
    require("esp_hf_client_register_callback" in call_relay and
            "esp_hf_ag_register_callback" in call_relay,
            "the calls adapter must contain both phone-facing and Tesla-facing HFP roles")
    require("runtime.hpp" not in calls_protocol and "esp_" not in calls_protocol,
            "calls/3 and call-audio codecs must remain portable protocol code")
    require("class Controller" in music_header and "AudioOwner" in music_header and
            "Controller::apply_remote_state" in music_source and
            "Controller::accept_remote_command" in music_source,
            "music state, replay protection, and audio focus must remain in the portable core")
    require("esp_a2d_sink_init" in music_adapter and "esp_a2d_source_init" in music_adapter and
            "music_core::Controller music_controller" in music_adapter,
            "the music adapter must implement both Bluetooth roles using the portable controller")
    require("music_set_call_active" in call_relay and "music_audio_allowed" in music_adapter,
            "call state must explicitly preempt music audio")
    require("!music_audio_allowed()" in call_audio_transport and
            "if (music_audio_allowed())" in call_audio_transport,
            "the shared audio transport must reject new music and drain queued music during calls")
    require("runtime.hpp" not in music_protocol and "esp_" not in music_protocol,
            "music/1 audio framing must remain portable protocol code")
    require("class Store" in contacts_header and "class TransferReceiver" in contacts_header and
            "class TransferSender" in contacts_header and "TransferReceiver::done" in contacts_source,
            "contact state and atomic transfer/session rules must remain in the portable core")
    require("class Parser" in contacts_protocol and "esp_" not in contacts_protocol and
            "runtime.hpp" not in contacts_protocol,
            "the contacts v1 vCard parser must remain a portable protocol codec")
    require("esp_pbac_register_callback" in contacts_adapter and
            "contact_core::TransferSender" in contacts_adapter and
            "contact_core::TransferReceiver" in contacts_adapter,
            "the contacts adapter must compose iPhone PBAP and compatibility transfer with portable state")
    require("class Server" in pbap_header and "x-bt/phonebook" in pbap_source and
            "X-IRMC-CALL-DATETIME" in pbap_source,
            "Tesla PBAP projection must remain in the car adapter")
    require("runtime.hpp" not in pbap_header + pbap_source and "esp_" not in pbap_header + pbap_source,
            "PBAP adapter must depend only on portable core and protocol code")
    require("pbap_adapter::Server pbap" in car_source and
            "pbap_adapter::Server::accepts_connect" in car_source,
            "Board B must compose the portable Tesla PBAP adapter")
    for message_type in ("Heartbeat", "Notification", "Call", "MusicState", "MusicCommand",
                         "ContactReset", "ContactEntry", "ContactDone", "ContactAck"):
        require(f"struct {message_type}" in dashlink_header,
                f"DashLink v2 is missing typed {message_type} traffic")
    require("using Packet = std::variant" in dashlink_header and
            "Bytes encode(const Packet &packet)" in dashlink_header and
            "class Decoder" in dashlink_header,
            "DashLink v2 must expose one typed packet variant and bounded codec")
    require("{'D', 'L', 2" in dashlink_source and
            "maximum_frame_size" in dashlink_source and "crc32" in dashlink_source,
            "DashLink v2 framing must keep its magic, version, size bound, and checksum")
    require("dashlink::Decoder decoder" in app_main and
            "std::get_if<dashlink::Call>" in app_main and
            "std::get_if<dashlink::Notification>" in app_main and
            "dashlink::is_call(packet)" in transport_source,
            "app and transport must decode and priority-route typed DashLink packets")
    require("dashlink::Call" in call_relay and ".notice" not in call_relay,
            "calls adapter must use named DashLink call fields")
    require("dashlink::MusicState" in music_adapter and
            "dashlink::MusicCommand" in music_adapter,
            "music metadata and controls must use typed DashLink messages")
    require("dashlink::ContactEntry" in contacts_adapter and
            "dashlink::ContactAck" in contacts_adapter,
            "contact transfer must use typed DashLink messages")
    require("UART_NUM_2" in platform_source and "UART_NUM_1" in call_audio_transport and
            "music_audio_transport_receive" in call_audio_transport,
            "typed control and real-time call/music audio must remain separate physical links")
    require("music::Audio" not in dashlink_header and "EncodedAudio" not in dashlink_header,
            "real-time media frames must not enter the DashLink control packet variant")
    require('#include "driver/' not in app_main and '#include "freertos/' not in app_main and
            '#include "esp_' not in app_main and '#include "nvs' not in app_main,
            "board composition must access ESP-IDF only through the platform component")
    require("dashbridge/apps" not in phone_source + car_source + call_relay + music_adapter + contacts_adapter,
            "adapters must not depend on the board app")
    require("esp_" not in transport_header + transport_source and
            "freertos/" not in transport_header + transport_source,
            "DashLink transport must remain portable")
    require("dashbridge/adapters" not in platform_source,
            "ESP platform must not depend on feature adapters")

    uuids = {"service": contract["service_uuid"]}
    uuids.update({name: value["uuid"] for name, value in contract["characteristics"].items()})
    for name, value in uuids.items():
        require(f'static let {name} = CBUUID(string: "{value}")' in swift,
                f"iOS setup {name} UUID differs from setup GATT v1 contract")
        match = re.search(
            rf"std::array<uint8_t,\s*16>\s+{name}_uuid\s*=\s*\{{([^}}]+)\}}",
            protocol_header,
        )
        require(match is not None, f"setup GATT v1 protocol {name} UUID is missing")
        if match:
            actual = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", match.group(1)))
            expected = UUID(value).bytes[::-1]
            require(actual == expected,
                    f"setup GATT v1 protocol {name} UUID differs from its contract")

    for name in contract["discovery_local_names"]:
        require(f'"{name}"' in swift, f'iOS no longer recognizes released local name "{name}"')

    status = contract["status"]
    require(f"data.count >= {status['length']}" in swift,
            "iOS setup status length differs from setup GATT v1 contract")
    require(f"data[0] == {status['version_byte']}" in swift,
            "iOS setup status version differs from setup GATT v1 contract")
    require(f"status_version = {status['version_byte']};" in protocol_header,
            "setup GATT v1 status version differs from its contract")
    require(f"status_size = {status['length']};" in protocol_header,
            "setup GATT v1 status length differs from its contract")

    core_status_fields = {
        "phoneBluetooth": "phone_bluetooth",
        "notifications": "notifications",
        "phoneCalls": "phone_calls",
        "internalLink": "internal_link",
        "teslaMessages": "tesla_messages",
        "teslaTransport": "tesla_transport",
        "teslaSync": "tesla_sync",
        "teslaCalls": "tesla_calls",
        "phonePairingOpen": "phone_pairing_open",
    }
    for property_name, bit in status["bits"].items():
        require(f"{property_name} = has({bit})" in swift,
                f"iOS status bit {property_name} differs from setup GATT v1 contract")
        field = core_status_fields[property_name]
        require(f"if (status.{field}) flags |= 1u << {bit};" in protocol_source,
                f"setup GATT v1 status bit {property_name} differs from its contract")

    maximum_command = contract["characteristics"]["command"]["maximum_length"]
    require(f"maximum_command_size = {maximum_command};" in protocol_header,
            "setup GATT v1 command size differs from its contract")
    require("ESP_GATT_PERM_WRITE_ENCRYPTED" in adapter and
            "protocol::maximum_command_size" in adapter,
            "setup GATT adapter command size or security differs from its contract")
    require("ESP_GATT_PERM_READ_ENCRYPTED" in adapter,
            "setup GATT adapter policy read is no longer encrypted")
    require("send(allowed ? 1 : 2" in swift,
            "iOS allow/deny command operations differ from setup GATT v1 contract")
    for operation in (3, 4):
        require(f"send({operation})" in all_swift,
                f"iOS command {operation} is no longer represented")
        require(f"operation == {operation}" in protocol_source,
                f"setup GATT v1 command {operation} is no longer represented")

if failures:
    for failure in failures:
        print(f"architecture: {failure}", file=sys.stderr)
    raise SystemExit(1)

print("Architecture boundaries, no legacy wrappers, typed DashLink v2, dual control/media links, setup GATT v1, MAP/PBAP/OBEX, calls, contacts, and music/audio focus passed")
