#!/usr/bin/env python3
"""Validate independent product versions and their shared compatibility contract."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
FIRMWARE_PATTERN = re.compile(
    r"\d+\.\d+\.\d+(?:-[A-Za-z0-9]+(?:\.[A-Za-z0-9]+)*)?"
)
IOS_PATTERN = re.compile(r"\d+(?:\.\d+){1,2}")


def setting(text: str, name: str) -> str:
    match = re.search(rf"^\s*{re.escape(name)}\s*=\s*(\S+)\s*$", text, re.MULTILINE)
    if not match:
        raise ValueError(f"Missing {name} in ios/Config/Version.xcconfig")
    return match.group(1)


def versions() -> tuple[str, str, str]:
    firmware = (ROOT / "firmware/VERSION").read_text().strip()
    if not FIRMWARE_PATTERN.fullmatch(firmware):
        raise ValueError(f"Invalid firmware version: {firmware!r}")

    ios_config = (ROOT / "ios/Config/Version.xcconfig").read_text()
    ios_marketing = setting(ios_config, "MARKETING_VERSION")
    ios_build = setting(ios_config, "CURRENT_PROJECT_VERSION")
    if not IOS_PATTERN.fullmatch(ios_marketing):
        raise ValueError(f"Invalid iOS marketing version: {ios_marketing!r}")
    if not ios_build.isdecimal() or int(ios_build) < 1:
        raise ValueError(f"Invalid iOS build number: {ios_build!r}")
    return firmware, ios_marketing, ios_build


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware-tag", help="Require firmware-v<VERSION>")
    parser.add_argument("--ios-tag", help="Require ios-v<VERSION>-b<BUILD>")
    args = parser.parse_args()

    firmware, ios_marketing, ios_build = versions()
    manifest = json.loads((ROOT / "dist/manifest.json").read_text())
    if manifest.get("version") != firmware:
        raise ValueError("dist/manifest.json does not match firmware/VERSION")

    firmware_notes = (ROOT / "docs/FIRMWARE_RELEASE.md").read_text()
    if f"# DashBridge firmware {firmware}" not in firmware_notes:
        raise ValueError("firmware release heading does not match firmware/VERSION")
    for role in ("phone", "car"):
        image = manifest.get("images", {}).get(role, {})
        image_version = image.get("version")
        image_hash = image.get("sha256")
        if not image_version or not image_hash:
            raise ValueError(f"manifest is missing release metadata for {role}")
        if f"`{image_version}`" not in firmware_notes or f"`{image_hash}`" not in firmware_notes:
            raise ValueError(f"firmware release page does not match the {role} image")

    contract = json.loads((ROOT / "contracts/setup_gatt_v1/contract.json").read_text())
    if contract.get("version") != 1:
        raise ValueError("setup_gatt_v1 must remain contract version 1")

    ios_notes = (ROOT / "docs/IOS_RELEASE.md").read_text()
    if f"# DashBridge iOS app {ios_marketing} (build {ios_build})" not in ios_notes:
        raise ValueError("iOS release heading does not match Version.xcconfig")

    compatibility = (ROOT / "contracts/COMPATIBILITY.md").read_text()
    if firmware not in compatibility or f"{ios_marketing} (build {ios_build})" not in compatibility:
        raise ValueError("compatibility matrix does not list both current releases")

    info = (ROOT / "ios/App/Info.plist").read_text()
    if "$(MARKETING_VERSION)" not in info or "$(CURRENT_PROJECT_VERSION)" not in info:
        raise ValueError("iOS Info.plist must read both values from Version.xcconfig")

    if args.firmware_tag and args.firmware_tag != f"firmware-v{firmware}":
        raise ValueError(
            f"Firmware tag {args.firmware_tag!r} must be firmware-v{firmware}"
        )
    expected_ios_tag = f"ios-v{ios_marketing}-b{ios_build}"
    if args.ios_tag and args.ios_tag != expected_ios_tag:
        raise ValueError(f"iOS tag {args.ios_tag!r} must be {expected_ios_tag}")

    print(
        f"Firmware {firmware}; iOS {ios_marketing} ({ios_build}); "
        "Setup GATT v1 compatibility passed"
    )


if __name__ == "__main__":
    main()
