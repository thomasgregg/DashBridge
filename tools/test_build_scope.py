"""Regression tests for board-specific builds and stale artifact detection."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from build_scope import select, sources


class BuildScopeTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ("main/phone.cpp", "main/car.cpp", "main/main.cpp",
                     "sdkconfig.phone", "sdkconfig.car", "sdkconfig.single", "sdkconfig.defaults",
                     "sdp_diagnostics/patch.py"):
            self.write("firmware/" + name, "original")
        manifest = {"images": {}, "target": "esp32", "flash_size": "4MB"}
        for role in ("phone", "car", "single"):
            name = f"{role}-merged.bin"
            self.write("dist/" + name, role)
            manifest["images"][role] = {
                "file": name, "flash_address": "0x0", "bytes": len(role),
                "sha256": hashlib.sha256(role.encode()).hexdigest(),
                "source_sha256": sources(self.root, role),
            }
        self.write("dist/manifest.json", json.dumps(manifest))

    def write(self, name, value):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(value)

    def test_current_images_skip_compilation(self):
        self.assertEqual(select(self.root), [])
        self.write("web/index.html", "new page")
        self.write("README.md", "new docs")
        self.assertEqual(select(self.root), [])

    def test_role_changes(self):
        for path, expected in (
            ("main/phone.cpp", ["phone", "single"]), ("sdkconfig.phone", ["phone"]),
            ("sdkconfig.single", ["single"]),
            ("main/car.cpp", ["car", "single"]), ("sdkconfig.car", ["car"]),
            ("sdp_diagnostics/patch.py", ["car", "single"]),
            ("main/main.cpp", ["phone", "car", "single"]),
            ("sdkconfig.defaults", ["phone", "car", "single"]),
        ):
            with self.subTest(path=path):
                self.write("firmware/" + path, "changed")
                self.assertEqual(select(self.root), expected)
                self.write("firmware/" + path, "original")

    def test_new_and_deleted_sources(self):
        self.write("firmware/main/new.hpp", "new dependency")
        self.assertEqual(select(self.root), ["phone", "car", "single"])
        (self.root / "firmware/main/new.hpp").unlink()
        (self.root / "firmware/main/car.cpp").unlink()
        self.assertEqual(select(self.root), ["car", "single"])

    def test_corrupt_and_missing_artifacts(self):
        self.write("dist/car-merged.bin", "bad")
        self.assertEqual(select(self.root), ["car"])
        (self.root / "dist/phone-merged.bin").unlink()
        self.assertEqual(select(self.root), ["phone", "car"])

    def test_manual_selection_forces_only_requested_roles(self):
        self.assertEqual(select(self.root, "phone"), ["phone"])
        self.assertEqual(select(self.root, "car"), ["car"])
        self.assertEqual(select(self.root, "single"), ["single"])
        self.assertEqual(select(self.root, "both"), ["phone", "car"])


if __name__ == "__main__":
    unittest.main()
