"""Execute the production reconnect polling block for each board role."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
source = (root / 'firmware/main/call_relay.cpp').read_text()
start = source.index('    // Reconnect control:')
end = source.index('    // Audio connection follows', start)
poll = source[start:end]
harness = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
using esp_err_t = int;
constexpr int ESP_OK = 0;
struct State { bool linked = false; } self;
bool peer_saved = true, connecting = false;
int64_t tick = 0, next_connect = 5000;
unsigned reconnect_delay = 5000, reconnect_attempts = 0;
int last_connect_result = ESP_OK, request_result = ESP_OK;
int client_opens = 0, client_closes = 0, gateway_opens = 0, gateway_closes = 0;
uint8_t peer[6] = {};
int64_t now() { return tick; }
int esp_hf_client_connect(uint8_t*) { ++client_opens; return request_result; }
int esp_hf_client_disconnect(uint8_t*) { ++client_closes; return ESP_OK; }
int esp_hf_ag_slc_connect(uint8_t*) { ++gateway_opens; return request_result; }
int esp_hf_ag_slc_disconnect(uint8_t*) { ++gateway_closes; return ESP_OK; }
void poll() {
POLL_CODE
}
int main() {
#if CONFIG_BRIDGE_PHONE
    auto &opens = client_opens; auto &closes = client_closes;
#else
    auto &opens = gateway_opens; auto &closes = gateway_closes;
#endif
    tick = 4999; poll(); assert(opens == 0);
    tick = 5000; poll(); assert(opens == 1 && connecting);
    tick = 24000; poll(); assert(opens == 1 && closes == 0);
    tick = 25000; poll(); assert(closes == 1 && !connecting);
    assert(next_connect == 30000);
    tick = 30000; request_result = 1; poll();
    assert(opens == 2 && !connecting && last_connect_result == 1);
    tick = 49999; poll(); assert(opens == 2);
    self.linked = true; tick = 70000; poll(); assert(opens == 2);
    self.linked = false; peer_saved = false; poll(); assert(opens == 2);
#if CONFIG_BRIDGE_PHONE
    assert(gateway_opens == 0 && gateway_closes == 0);
#else
    assert(client_opens == 0 && client_closes == 0);
#endif
}

'''.replace('POLL_CODE', poll)
with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary)
    (path / 'test.cpp').write_text(harness)
    for phone in (0, 1):
        binary = path / f'test-{phone}'
        subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', f'-DCONFIG_BRIDGE_PHONE={phone}',
                        str(path / 'test.cpp'), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
print('PASS reconnect policy: timed retries, timeout cancellation and correct A/B Bluetooth role')
