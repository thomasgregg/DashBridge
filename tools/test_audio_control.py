"""Replay production diagnostic controls: routing, acknowledgement and stale-call rejection."""
import os
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parent.parent
source = (root / 'firmware/main/call_relay.cpp').read_text()
controls = source[source.index('static void apply_audio_test('):source.index('template<class Stats>')]
receive = source[source.index('    if (m.notice.title == "audio-test"'):source.index('    if ((m.notice.title == "audio"')]
harness = r'''
#include "call_protocol.hpp"
#include "call_audio_test.hpp"
#include <cassert>
#include <vector>
#include <iostream>
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
using bridge::WireMessage; using bridge::Op;
using Mode = calls::AudioTestMode;
static calls::State self, other;
static calls::CommandGate test_commands;
static uint32_t boot=10, other_boot=20, test_sequence=0, test_pending=0;
static int64_t test_deadline=0;
static bool live=true;
static Mode applied=Mode::normal;
static unsigned applications=0;
static std::vector<WireMessage> sent;
int64_t now(){return 1000;}
bool peer_live(){return live;}
void transmit(const WireMessage &m){sent.push_back(m);}
WireMessage message(const char *kind){WireMessage m{Op::call,boot,{}};m.notice.app=calls::protocol;m.notice.title=kind;return m;}
bool call_audio_test(Mode mode){if(mode!=Mode::normal&&!self.audio)return false;applied=mode;++applications;return true;}
'''
# Keep production calls (and their side effects) while suppressing host logging.
controls = controls.replace('bool ok = call_audio_test(mode);', '[[maybe_unused]] bool ok = call_audio_test(mode);')
harness += controls + '\nvoid receive(const WireMessage &m){\n' + receive + '\n}\n'
harness += r'''
int main(){
 self.audio=111;other.audio=222;test_commands.reset(other_boot);
 // Both roles can select either side; the non-selected side is reset to normal.
 relay_audio_test(bool(CONFIG_BRIDGE_PHONE),Mode::tone);
 assert(applied==Mode::tone && sent.back().notice.body=="normal");
 relay_audio_test(!bool(CONFIG_BRIDGE_PHONE),Mode::loopback);
 assert(applied==Mode::normal && sent.back().notice.body=="loopback" && sent.back().notice.date=="222");
 WireMessage ack{Op::call,other_boot,{}};
 ack.notice.title="audio-test-result";ack.notice.id=test_pending;ack.notice.date="10";ack.notice.body="loopback applied";
 receive(ack);assert(!test_pending);
 WireMessage m{Op::call,other_boot,{}};
 m.notice.title="audio-test";m.notice.id=1;m.notice.date="111";m.notice.body="loopback";
 receive(m);assert(applied==Mode::loopback && sent.back().notice.body=="loopback applied");
 auto before=applications;receive(m);assert(applications==before); // Duplicate cannot extend timeout.
 m.notice.id=2;m.notice.date="110";receive(m);
 assert(applications==before && sent.back().notice.body.find("rejected")!=std::string::npos);
 m.notice.id=3;m.notice.date="111";m.session=999;receive(m);assert(applications==before);
 m.session=other_boot;live=false;receive(m);assert(applications==before);
 live=true;m.notice.body="invalid";receive(m);assert(applications==before);
 self.audio=0;m.notice.id=4;m.notice.body="tone";receive(m);assert(applications==before);
 m.notice.id=5;m.notice.body="normal";receive(m);assert(applied==Mode::normal && applications==before+1);
 relay_audio_test_stop();assert(applied==Mode::normal && sent.back().notice.body=="normal");
 auto pending=test_pending;ack.notice.id=pending;ack.notice.date="999";receive(ack);assert(test_pending==pending);
 ack.notice.date="10";ack.session=999;receive(ack);assert(test_pending==pending);
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
                        '-I',str(root/'firmware/main'),'-I',str(root/'firmware/components/bridge_core/include'),
                        str(path/'test.cpp'),'-o',str(path/'test')],check=True)
        subprocess.run([str(path/'test')],check=True)
