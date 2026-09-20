"""Create merged ESP32 images from ESP-IDF's actual flash layout; no hardware writes."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from build_scope import sources, ROLES

ROOT = Path(__file__).resolve().parent.parent


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def package(roles):
    destination = ROOT / "dist"
    destination.mkdir(exist_ok=True)
    manifest_path = destination / "manifest.json"
    manifest = (json.loads(manifest_path.read_text())
                if manifest_path.exists() and roles != ["single"] else {"images": {}})
    for role in roles:
        if role not in ROLES:
            raise SystemExit("Roles must be phone, car or single")
        build = ROOT / "build" / role
        layout = json.loads((build / "flasher_args.json").read_text())
        if layout["extra_esptool_args"]["chip"] != "esp32":
            raise SystemExit("Unexpected chip target")
        output = destination / f"{role}-merged.bin"
        command = [sys.executable, "-m", "esptool", "--chip", "esp32", "merge_bin",
                   "-o", str(output)] + layout["write_flash_args"]
        for offset, filename in sorted(layout["flash_files"].items(), key=lambda x: int(x[0], 0)):
            command.extend((offset, str(build / filename)))
        subprocess.run(command, check=True)
        manifest["images"][role] = {
            "file": output.name, "flash_address": "0x0", "sha256": sha256(output),
            "bytes": output.stat().st_size, "source_sha256": sources(ROOT, role),
            "sdkconfig_sha256": sha256(build / "sdkconfig"),
            "hardware_tested": False, "version": "0.2.1-dev",
        }
    manifest.update({"project": "DashBridge prototype", "version": "0.2.1-dev",
                     "target": "esp32", "flash_size": "4MB", "esp_idf": "v5.5.1",
                     "esp_idf_commit": "fcae32885b0296b32044cb99ecbdc50d98dddb83"})
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    package(sys.argv[1:] or ["single"])
