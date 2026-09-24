"""Regression tests for board-specific builds and stale artifact detection."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from build_scope import select, sources, current, ROLES, firmware_version


class BuildScopeTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.write("version.txt", "0.3.1-alpha")
        for name in ("adapters/phone/esp_ancs_adapter/esp_ancs_adapter.cpp",
                     "adapters/car/esp_tesla_adapter/esp_tesla_adapter.cpp",
                     "apps/dashbridge_app/app_main.cpp",
                     "sdkconfig.phone", "sdkconfig.car", "sdkconfig.single", "sdkconfig.defaults",
                     "sdp_diagnostics/patch.py", "audio_codec/patch.py"):
            self.write("firmware/" + name, "original")
        manifest = {"images": {}, "target": "esp32", "flash_size": "4MB"}
        for role in ("phone", "car", "single"):
            name = f"{role}-merged.bin"
            self.write("dist/" + name, role)
            manifest["images"][role] = {
                "file": name, "flash_address": "0x0", "bytes": len(role),
                "sha256": hashlib.sha256(role.encode()).hexdigest(),
                "source_sha256": sources(self.root, role),
                "version": firmware_version(self.root, role),
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
            ("adapters/phone/esp_ancs_adapter/esp_ancs_adapter.cpp", ["phone", "single"]), ("sdkconfig.phone", ["phone"]),
            ("sdkconfig.single", ["single"]),
            ("adapters/car/esp_tesla_adapter/esp_tesla_adapter.cpp", ["car", "single"]), ("sdkconfig.car", ["car"]),
            ("sdp_diagnostics/patch.py", ["car", "single"]),
            ("audio_codec/patch.py", ["phone", "car", "single"]),
            ("apps/dashbridge_app/app_main.cpp", ["phone", "car", "single"]),
            ("sdkconfig.defaults", ["phone", "car", "single"]),
        ):
            with self.subTest(path=path):
                self.write("firmware/" + path, "changed")
                self.assertEqual([r for r in ROLES if not current(self.root, r)], expected)
                self.assertEqual(select(self.root), ["single"] if "single" in expected else [])
                self.write("firmware/" + path, "original")

    def test_new_and_deleted_sources(self):
        self.write("firmware/core/new.hpp", "new dependency")
        self.assertEqual(select(self.root), ["single"])
        (self.root / "firmware/core/new.hpp").unlink()
        (self.root / "firmware/adapters/car/esp_tesla_adapter/esp_tesla_adapter.cpp").unlink()
        self.assertEqual(select(self.root), ["single"])

    def test_corrupt_and_missing_artifacts(self):
        self.write("dist/car-merged.bin", "bad")
        self.assertEqual(select(self.root), [])
        (self.root / "dist/phone-merged.bin").unlink()
        self.assertEqual(select(self.root), [])
        (self.root / "dist/single-merged.bin").unlink()
        self.assertEqual(select(self.root), ["single"])

    def test_relay_selects_only_stale_two_board_images(self):
        self.write("firmware/sdkconfig.phone", "CONFIG_BRIDGE_CALL_RELAY=y")
        self.assertEqual(select(self.root), ["phone"])
        self.write("firmware/adapters/car/esp_tesla_adapter/esp_tesla_adapter.cpp", "changed")
        self.assertEqual(select(self.root), ["phone", "car"])

    def test_manual_selection_forces_only_requested_roles(self):
        self.assertEqual(select(self.root, "phone"), ["phone"])
        self.assertEqual(select(self.root, "car"), ["car"])
        self.assertEqual(select(self.root, "single"), ["single"])
        self.assertEqual(select(self.root, "both"), ["phone", "car"])

    def test_version_tracks_only_relevant_firmware_inputs(self):
        before = {r: firmware_version(self.root, r) for r in ROLES}
        self.write("README.md", "docs only")
        self.write("web/app.js", "web only")
        self.assertEqual(before, {r: firmware_version(self.root, r) for r in ROLES})
        self.write("firmware/adapters/car/esp_tesla_adapter/esp_tesla_adapter.cpp", "new B code")
        self.assertEqual(before['phone'], firmware_version(self.root, 'phone'))
        self.assertNotEqual(before['car'], firmware_version(self.root, 'car'))
        self.write("firmware/adapters/car/esp_tesla_adapter/esp_tesla_adapter.cpp", "original")
        self.assertEqual(before['car'], firmware_version(self.root, 'car'))
        self.write("version.txt", "0.3.2-alpha")
        for role in ROLES:
            self.assertTrue(firmware_version(self.root, role).startswith('0.3.2-alpha+'))
            self.assertLessEqual(len(firmware_version(self.root, role)), 31)

    def test_tool_changes_and_relabelled_images_are_stale(self):
        self.write("tools/package.py", "changed packaging")
        self.assertTrue(all(not current(self.root, r) for r in ROLES))
        (self.root / 'tools/package.py').unlink()
        filename = self.root / 'dist/manifest.json'
        manifest = json.loads(filename.read_text())
        manifest['images']['car']['version'] = 'made-up-version'
        filename.write_text(json.dumps(manifest))
        self.assertFalse(current(self.root, 'car'))
        self.assertTrue(current(self.root, 'phone'))


if __name__ == "__main__":
    unittest.main()
