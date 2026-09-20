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
    tick = 4999; poll(); assert(client_opens == 0);
    tick = 5000; poll(); assert(client_opens == 1 && connecting);
    tick = 24000; poll(); assert(client_opens == 1 && client_closes == 0);
    tick = 25000; poll(); assert(client_closes == 1 && !connecting);
    assert(next_connect == 30000);
    tick = 30000; request_result = 1; poll();
    assert(client_opens == 2 && !connecting && last_connect_result == 1);
    self.linked = true; tick = 70000; poll(); assert(client_opens == 2);
    self.linked = false; peer_saved = false; poll(); assert(client_opens == 2);
#else
    // Boot, delayed car wake, long absence, incoming connection and loss must
    // never initiate/cancel a gateway link or run the phone reconnect path.
    for (bool saved : {false, true}) {
        peer_saved = saved;
        for (bool linked : {false, true, false}) {
            self.linked = linked;
            for (int64_t t : {0LL, 5000LL, 25000LL, 60000LL, 3600000LL}) {
                tick = t; poll();
            }
        }
    }
    assert(client_opens == 0 && client_closes == 0 && reconnect_attempts == 0);
#endif
    assert(gateway_opens == 0 && gateway_closes == 0);
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
print('PASS reconnect policy: A retains timed retries; B never initiates or cancels the Tesla phone link')
