"""Replay production diagnostic controls: routing, acknowledgement and stale-call rejection."""
import os
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parent.parent
source = (root / 'firmware/adapters/calls/esp_hfp_adapter/esp_hfp_adapter.cpp').read_text()
controls = source[source.index('static void apply_audio_test('):source.index('template<class Stats>')]
receive = source[source.index('    if (m.kind == "audio-test"'):source.index('    if ((m.kind == "audio"')]
harness = r'''
#include "dashbridge/protocols/calls_v3.hpp"
#include "dashbridge/protocols/dashlink_v2.hpp"
#include "dashbridge/adapters/call_audio_test.hpp"
#include <cassert>
#include <vector>
#include <iostream>
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
namespace dashlink = dashbridge::protocols::dashlink_v2;
using Mode = calls::AudioTestMode;
static calls::Controller call_state;
static calls::State &self=call_state.local(), &other=call_state.remote();
static calls::CommandGate &test_commands=call_state.test_commands();
static uint32_t boot=10, other_boot=20, test_sequence=0, test_pending=0;
static int64_t test_deadline=0;
static bool live=true;
static Mode applied=Mode::normal;
static unsigned applications=0;
static std::vector<dashlink::Call> sent;
int64_t now(){return 1000;}
bool peer_live(){return live;}
void transmit(const dashlink::Call &m){sent.push_back(m);}
dashlink::Call message(const char *kind){return {boot,0,kind,{},{},{}};}
bool call_audio_test(Mode mode){if(mode!=Mode::normal&&!self.audio)return false;applied=mode;++applications;return true;}
'''
# Keep production calls (and their side effects) while suppressing host logging.
controls = controls.replace('bool ok = call_audio_test(mode);', '[[maybe_unused]] bool ok = call_audio_test(mode);')
harness += controls + '\nvoid receive(const dashlink::Call &m){\n' + receive + '\n}\n'
harness += r'''
int main(){
 self.audio=111;calls::State remote;remote.audio=222;
 assert(call_state.apply_remote_snapshot(other_boot,1,remote)==calls::SnapshotResult::new_session);
 // Both roles can select either side; the non-selected side is reset to normal.
 relay_audio_test(bool(CONFIG_BRIDGE_PHONE),Mode::tone);
 assert(applied==Mode::tone && sent.back().payload=="normal");
 relay_audio_test(!bool(CONFIG_BRIDGE_PHONE),Mode::loopback);
 assert(applied==Mode::normal && sent.back().payload=="loopback" && sent.back().destination=="222");
 dashlink::Call ack{other_boot,test_pending,"audio-test-result","loopback applied",{},"10"};
 receive(ack);assert(!test_pending);
 dashlink::Call m{other_boot,1,"audio-test","loopback",{},"111"};
 receive(m);assert(applied==Mode::loopback && sent.back().payload=="loopback applied");
 auto before=applications;receive(m);assert(applications==before); // Duplicate cannot extend timeout.
 m.sequence=2;m.destination="110";receive(m);
 assert(applications==before && sent.back().payload.find("rejected")!=std::string::npos);
 m.sequence=3;m.destination="111";m.session=999;receive(m);assert(applications==before);
 m.session=other_boot;live=false;receive(m);assert(applications==before);
 live=true;m.payload="invalid";receive(m);assert(applications==before);
 self.audio=0;m.sequence=4;m.payload="tone";receive(m);assert(applications==before);
 m.sequence=5;m.payload="normal";receive(m);assert(applied==Mode::normal && applications==before+1);
 relay_audio_test_stop();assert(applied==Mode::normal && sent.back().payload=="normal");
 auto pending=test_pending;ack.sequence=pending;ack.destination="999";receive(ack);assert(test_pending==pending);
 ack.destination="10";ack.session=999;receive(ack);assert(test_pending==pending);
 ack.session=other_boot;receive(ack);assert(!test_pending);
 std::cout << "PASS production diagnostic controls role " << CONFIG_BRIDGE_PHONE << ": local/remote routing, stop, ACK, idle/stale/replayed commands\n";
}
'''
with tempfile.TemporaryDirectory() as temporary:
    path=Path(temporary)
    (path/'test.cpp').write_text(harness)
    for phone in (0,1):
        subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-Wall','-Wextra','-Werror',
                        '-fsanitize=address,undefined',f'-DCONFIG_BRIDGE_PHONE={phone}',
                        '-I',str(root/'firmware/adapters/dashbridge_adapter_api/include'),
                        '-I',str(root/'firmware/adapters/calls/esp_hfp_adapter/include'),
                        '-I',str(root/'firmware/core/dashbridge_domain_core/include'),
                        '-I',str(root/'firmware/protocols/calls_v3/include'),
                        '-I',str(root/'firmware/protocols/dashlink_v2/include'),
                        str(root/'firmware/core/dashbridge_domain_core/calls.cpp'),
                        str(root/'firmware/core/dashbridge_domain_core/text.cpp'),
                        str(path/'test.cpp'),'-o',str(path/'test')],check=True)
        subprocess.run([str(path/'test')],check=True)
