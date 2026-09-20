"""Validate the production SDP record with the pinned SDK's actual validator.

Run after a firmware build, with ESP-IDF's environment active. This is a host
contract test; it neither opens a serial port nor exercises a Bluetooth radio.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
SDK = Path(os.environ["IDF_PATH"])


def section(path, start, end):
    source = path.read_text()
    if source.count(start) != 1 or source.count(end) != 1:
        raise RuntimeError(f"Review SDP test extraction after changes to {path}")
    return source[source.index(start):source.index(end)]


validator = section(
    SDK / "components/bt/host/bluedroid/api/esp_sdp_api.c",
    "static bool esp_sdp_record_integrity_check(",
    "\nesp_err_t esp_sdp_register_callback(",
)
record = section(ROOT / "firmware/main/car.cpp",
                 "static void record() {", "\nstatic void drop_mns()")
harness = r'''
#include <cstring>
#include <cassert>
#include "esp_sdp_api.h"
#define LOG_ERROR(...) ((void)0)
#undef ESP_ERROR_CHECK
#define ESP_ERROR_CHECK(expr) assert((expr) == ESP_OK)
static bool sdp_ready = true, record_created = false;
static uint8_t channel_number = 4;
''' + validator + r'''
extern "C" esp_err_t esp_sdp_create_record(esp_bluetooth_sdp_record_t *r) {
    assert(esp_sdp_record_integrity_check(r));
    // The original crash: excluding the NUL must fail the real SDK validator.
    r->hdr.service_name_length--;
    assert(!esp_sdp_record_integrity_check(r));
    return ESP_OK;
}
''' + record + '\nint main() { record(); assert(record_created); }\n'

with tempfile.TemporaryDirectory(prefix="dashbridge-sdp-") as temp:
    source = Path(temp) / "check.cpp"
    binary = Path(temp) / "check"
    source.write_text(harness)
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined",
        "-I", str(ROOT / "build" / os.environ.get("DASHBRIDGE_BUILD_ROLE", "car") / "config"),
        "-I", str(SDK / "components/esp_common/include"),
        "-I", str(SDK / "components/bt/host/bluedroid/api/include/api"),
        str(source), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS production SDP record accepted; original name-length error rejected")
