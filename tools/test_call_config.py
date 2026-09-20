"""Verify the actual generated Kconfig selects the intended HFP role and PCM path."""
from pathlib import Path
import os
import json

role = os.environ["DASHBRIDGE_BUILD_ROLE"]
root = Path(__file__).resolve().parent.parent
text = (root / "build" / role / "sdkconfig").read_text()
expected = {
    "BRIDGE_CALL_RELAY": "y", "BT_HFP_ENABLE": "y",
    "BT_HFP_AUDIO_DATA_PATH_HCI": "y", "FREERTOS_HZ": "1000",
    "BT_HFP_WBS_ENABLE": None, "BT_HFP_USE_EXTERNAL_CODEC": None,
    "BT_HFP_CLIENT_ENABLE": "y" if role == "phone" else None,
    "BT_HFP_AG_ENABLE": "y" if role == "car" else None,
}
for key, value in expected.items():
    line = f"CONFIG_{key}={value}" if value else f"# CONFIG_{key} is not set"
    assert line in text.splitlines(), f"Wrong {role} setting: expected {line}"
if role == "car":
    commands = json.loads((root / "build" / role / "compile_commands.json").read_text())
    source = next(item for item in commands if item["file"].endswith("/btc_hf_ag.c"))
    definition = "BTC_HF_FEATURES=(BTA_AG_FEAT_REJECT|BTA_AG_FEAT_EXTERR|BTA_AG_FEAT_ESCO_S4|BTA_AG_FEAT_UNAT)"
    assert definition in source["command"], "Gateway advertises features outside this prototype"
print(f"PASS {role}: correct HFP role, internal CVSD/PCM codec and audio worker tick")
