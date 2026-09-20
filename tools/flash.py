"""Flash an explicitly selected development board using a verified local image."""
import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


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
