"""Verify the actual generated Kconfig selects the intended HFP role and PCM path."""
from pathlib import Path
import os
import json
import shlex

role = os.environ["DASHBRIDGE_BUILD_ROLE"]
root = Path(__file__).resolve().parent.parent
text = (root / "build" / role / "sdkconfig").read_text()
expected = {
    "BRIDGE_CALL_RELAY": "y", "BT_HFP_ENABLE": "y",
    "BT_HFP_AUDIO_DATA_PATH_HCI": "y", "FREERTOS_HZ": "1000",
    "BT_HFP_WBS_ENABLE": "y",
    "BT_HFP_USE_EXTERNAL_CODEC": "y",
    "BRIDGE_MUSIC_RELAY": "y",
    "BT_A2DP_ENABLE": "y",
    "BT_A2DP_USE_EXTERNAL_CODEC": "y",
    "BRIDGE_CONTACT_SYNC": "y",
    "BT_PBAC_ENABLED": "y" if role == "phone" else None,
    "BT_GOEPC_ENABLED": "y",
    "BT_HFP_CLIENT_ENABLE": "y" if role == "phone" else None,
    "BT_HFP_AG_ENABLE": "y" if role == "car" else None,
    "BTDM_CTRL_MODE_BR_EDR_ONLY": "y" if role == "car" else None,
    "BTDM_CTRL_MODE_BTDM": "y" if role == "phone" else None,
    "BT_BLE_ENABLED": "y" if role == "phone" else None,
}
for key, value in expected.items():
    line = f"CONFIG_{key}={value}" if value else f"# CONFIG_{key} is not set"
    assert line in text.splitlines(), f"Wrong {role} setting: expected {line}"
if role == "car":
    platform = (root / "firmware" / "platform" / "esp_runtime" / "esp_runtime.cpp").read_text()
    assert "esp_bt_controller_mem_release(ESP_BT_MODE_BLE)" in platform
    assert "esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)" in platform
    assert "Board B controller mode: Classic only (HFP + MAP)" in platform
    commands = json.loads((root / "build" / role / "compile_commands.json").read_text())
    source = next(item for item in commands if item["file"].endswith("/btc_hf_ag.c"))
    assert not any(item.startswith("-DBTC_HF_FEATURES=") for item in shlex.split(source["command"])), \
        "Car build must retain the stock ESP-IDF HFP gateway feature advertisement"
print(f"PASS {role}: correct HFP/A2DP roles and external codec-transparent audio paths")
