"""Check the built experiment excludes both HFP roles and their public APIs."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent.parent
build = root / "build/single"
config = (build / "sdkconfig").read_text().splitlines()
for feature in ("CONFIG_BT_HFP_ENABLE=y", "CONFIG_BT_HFP_AG_ENABLE=y", "CONFIG_BT_HFP_CLIENT_ENABLE=y"):
    if feature in config:
        raise SystemExit(f"Unexpected phone profile enabled: {feature}")
symbols = subprocess.check_output(["xtensa-esp32-elf-nm", "--defined-only", str(build / "dashbridge.elf")], text=True)
if any(" esp_hf_" in line for line in symbols.splitlines()):
    raise SystemExit("Unexpected HFP API in linked firmware")
for symbol in ("esp_sdp_create_record", "esp_spp_start_srv", "esp_ble_gattc_app_register"):
    if not any(line.endswith(" " + symbol) for line in symbols.splitlines()):
        raise SystemExit(f"Required message transport missing: {symbol}")
print("PASS linked image retains MAP transport and BLE client with no HFP roles or APIs")
