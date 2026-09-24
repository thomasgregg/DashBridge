"""Flash an explicitly selected development board using a verified local image."""
import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path
import subprocess
import sys
from build_scope import current

ROOT = Path(__file__).resolve().parent.parent


def require_current_source(board):
    try:
        remote = subprocess.check_output(
            ["git", "ls-remote", "origin", "refs/heads/main"],
            cwd=ROOT, text=True, stderr=subprocess.PIPE).split()[0]
        tracking = subprocess.check_output(
            ["git", "rev-parse", "refs/remotes/origin/main"],
            cwd=ROOT, text=True, stderr=subprocess.PIPE).strip()
    except (subprocess.CalledProcessError, IndexError):
        raise SystemExit("Could not confirm GitHub main. Check your connection; nothing was flashed.")
    if remote != tracking:
        raise SystemExit("GitHub main has changed. Fetch the latest code before flashing.")
    if subprocess.run(["git", "merge-base", "--is-ancestor", remote, "HEAD"],
                      cwd=ROOT, check=False).returncode != 0:
        raise SystemExit("This checkout is behind GitHub main. Update it before flashing.")
    if not current(ROOT, board):
        raise SystemExit("The packaged image does not match this source. Rebuild it before flashing.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="List serial ports without writing")
    parser.add_argument("--board", choices=("phone", "car"))
    parser.add_argument("--port", help="Explicit serial device, e.g. /dev/cu.usbserial-0001")
    args = parser.parse_args()
    try:
        version = importlib.metadata.version("esptool")
    except importlib.metadata.PackageNotFoundError:
        parser.error("Install esptool==4.12.0 in a Python virtual environment first.")
    if version != "4.12.0":
        parser.error(f"Expected esptool 4.12.0; found {version}.")
    if args.list:
        from serial.tools.list_ports import comports
        for port in comports():
            print(f"{port.device}: {port.description}")
        return
    if not args.board or not args.port:
        parser.error("Specify both --board and --port, or use --list.")
    require_current_source(args.board)
    manifest = json.loads((ROOT / "dist" / "manifest.json").read_text())
    entry = manifest["images"][args.board]
    image = ROOT / "dist" / entry["file"]
    digest = hashlib.sha256(image.read_bytes()).hexdigest()
    if digest != entry["sha256"]:
        raise SystemExit("Firmware checksum mismatch; image not flashed.")
    print(f"Loading {args.board} firmware onto {args.port}. Existing board pairing will be erased.", flush=True)
    subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32", "--port", args.port,
                    "--baud", "115200", "write_flash", "--flash_mode", "dio",
                    "--flash_freq", "40m", "--flash_size", "4MB", "0x0", str(image)], check=True)


if __name__ == "__main__":
    main()
