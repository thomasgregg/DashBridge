"""Create merged ESP32 images from ESP-IDF's actual flash layout; no hardware writes."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from build_scope import sources, ROLES, firmware_version, release_version

ROOT = Path(__file__).resolve().parent.parent


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def package(roles):
    destination = ROOT / "dist"
    destination.mkdir(exist_ok=True)
    manifest_path = destination / "manifest.json"
    manifest = (json.loads(manifest_path.read_text())
                if manifest_path.exists() and roles != ["single"] else {"images": {}})
    if set(roles) <= {"phone", "car"}:
        manifest["images"] = {key: value for key, value in manifest["images"].items()
                              if key in ("phone", "car")}
    for role in roles:
        if role not in ROLES:
            raise SystemExit("Roles must be phone, car or single")
        build = ROOT / "build" / role
        layout = json.loads((build / "flasher_args.json").read_text())
        if layout["extra_esptool_args"]["chip"] != "esp32":
            raise SystemExit("Unexpected chip target")
        version = firmware_version(ROOT, role)
        app = (build / layout["app"]["file"]).read_bytes()
        # esp_app_desc_t follows the 24-byte image header and 8-byte segment header.
        if (int.from_bytes(app[32:36], "little") != 0xabcd5432
                or app[48:80].split(b"\0", 1)[0].decode("ascii") != version):
            raise SystemExit(f"Embedded {role} version differs from source; rebuild before packaging")
        if int(layout["app"]["offset"], 0) != 0x10000:
            raise SystemExit("Unexpected application flash offset")
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
            "hardware_tested": False, "version": version,
            "esp_idf": "v5.5.5",
            "esp_idf_commit": "b774170ff46c393eeb5e495ea37936038d3f4f4f",
        }
    manifest.update({"project": "DashBridge prototype", "version": release_version(ROOT),
                     "target": "esp32", "flash_size": "4MB", "esp_idf": "v5.5.5",
                     "esp_idf_commit": "b774170ff46c393eeb5e495ea37936038d3f4f4f"})
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    package(sys.argv[1:] or ["phone", "car"])
