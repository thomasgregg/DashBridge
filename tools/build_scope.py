"""Select stale board images using their own source and binary checksums."""
import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ROLES = ("phone", "car")
BUILD_INPUTS = ("tools/build_scope.py", "tools/build.sh", "tools/package.py")


def sources(root, role):
    excluded = {f"firmware/sdkconfig.{other}" for other in ROLES if other != role}
    other = "car" if role == "phone" else "phone"
    excluded.add(f"firmware/adapters/{'car/esp_tesla_adapter/esp_tesla_adapter' if other == 'car' else 'phone/esp_ancs_adapter/esp_ancs_adapter'}.cpp")
    result = {}
    for p in sorted((root / "firmware").rglob("*")):
        name = p.relative_to(root).as_posix()
        if name in excluded:
            continue
        if p.is_file() and (p.suffix in (".c", ".cpp", ".h", ".hpp", ".txt", ".py", ".cmake", ".csv", ".yml", ".yaml")
                            or p.name.startswith("sdkconfig.") or p.name.startswith("Kconfig")):
            result[name] = hashlib.sha256(p.read_bytes()).hexdigest()
    for name in BUILD_INPUTS:
        if (root / name).is_file():
            result[name] = hashlib.sha256((root / name).read_bytes()).hexdigest()
    return dict(sorted(result.items()))


def release_version(root):
    value = (root / "version.txt").read_text().strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+(?:-[A-Za-z0-9]+(?:\.[A-Za-z0-9]+)*)?", value):
        raise ValueError("Invalid firmware release version")
    return value


def firmware_version(root, role):
    if role not in ROLES:
        raise ValueError("Firmware version requires a board role")
    identity = json.dumps(sources(root, role), sort_keys=True, separators=(",", ":"))
    build_id = hashlib.sha256(identity.encode()).hexdigest()[:12]
    version = f"{release_version(root)}+{build_id}"
    if len(version.encode()) > 31:
        raise ValueError("Firmware version exceeds ESP-IDF's 31-byte limit")
    return version


def current(root, role):
    try:
        manifest = json.loads((root / "dist/manifest.json").read_text())
        entry = manifest["images"][role]
        if manifest["target"] != "esp32" or manifest["flash_size"] != "4MB":
            return False
        if entry["file"] != f"{role}-merged.bin" or entry["flash_address"] != "0x0":
            return False
        data = (root / "dist" / entry["file"]).read_bytes()
        return (entry.get("version") == firmware_version(root, role)
                and entry["source_sha256"] == sources(root, role)
                and entry["bytes"] == len(data)
                and entry["sha256"] == hashlib.sha256(data).hexdigest())
    except (OSError, ValueError, KeyError, TypeError):
        return False


def select(root, requested="auto"):
    if requested == "both":
        return ["phone", "car"]
    if requested in ROLES:
        return [requested]
    return [role for role in ROLES if not current(root, role)]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("board", nargs="?", default="auto", choices=("auto", "both", *ROLES))
    parser.add_argument("--github-output", type=Path)
    parser.add_argument("--version", action="store_true", help="Print the selected board's embedded firmware version")
    args = parser.parse_args()
    if args.version:
        print(firmware_version(ROOT, args.board))
        raise SystemExit(0)
    roles = select(ROOT, args.board)
    if args.github_output:
        with args.github_output.open("a") as output:
            output.write(f"roles={json.dumps(roles)}\n")
    print(" ".join(roles))
