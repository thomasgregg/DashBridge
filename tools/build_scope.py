"""Select stale board images using their own source and binary checksums."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ROLES = ("phone", "car", "single")


def sources(root, role):
    excluded = {f"firmware/sdkconfig.{other}" for other in ROLES if other != role}
    if role != "single":
        other = "car" if role == "phone" else "phone"
        excluded.add(f"firmware/main/{other}.cpp")
    result = {}
    for p in sorted((root / "firmware").rglob("*")):
        name = p.relative_to(root).as_posix()
        if name in excluded or (role == "phone" and name.startswith("firmware/sdp_diagnostics/")):
            continue
        if p.is_file() and (p.suffix in (".c", ".cpp", ".h", ".hpp", ".txt", ".py", ".cmake", ".csv", ".yml", ".yaml")
                            or p.name.startswith("sdkconfig.") or p.name.startswith("Kconfig")):
            result[name] = hashlib.sha256(p.read_bytes()).hexdigest()
    return result


def current(root, role):
    try:
        manifest = json.loads((root / "dist/manifest.json").read_text())
        entry = manifest["images"][role]
        if manifest["target"] != "esp32" or manifest["flash_size"] != "4MB":
            return False
        if entry["file"] != f"{role}-merged.bin" or entry["flash_address"] != "0x0":
            return False
        data = (root / "dist" / entry["file"]).read_bytes()
        return (entry["source_sha256"] == sources(root, role)
                and entry["bytes"] == len(data)
                and entry["sha256"] == hashlib.sha256(data).hexdigest())
    except (OSError, ValueError, KeyError, TypeError):
        return False


def select(root, requested="auto"):
    if requested == "both":
        return ["phone", "car"]
    if requested in ROLES:
        return [requested]
    relay = (root / "firmware/sdkconfig.phone").exists() and "CONFIG_BRIDGE_CALL_RELAY=y" in (root / "firmware/sdkconfig.phone").read_text()
    candidates = ("single",) if not relay and (root / "firmware/sdkconfig.single").exists() else ("phone", "car")
    return [role for role in candidates if not current(root, role)]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("board", nargs="?", default="auto", choices=("auto", "both", *ROLES))
    parser.add_argument("--github-output", type=Path)
    args = parser.parse_args()
    roles = select(ROOT, args.board)
    if args.github_output:
        with args.github_output.open("a") as output:
            output.write(f"roles={json.dumps(roles)}\n")
    print(" ".join(roles))
