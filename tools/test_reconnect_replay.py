"""Run real peer-storage and reconnect code with deterministic events and a virtual clock."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
source = (root / 'firmware/adapters/calls/esp_hfp_adapter/esp_hfp_adapter.cpp').read_text()
def function(signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]
poll = source[source.index('    // Reconnect policy:'):source.index('    // Audio connection follows')]
fixture = json.loads((root / 'tests/fixtures/passive-reconnect-events.json').read_text())
harness = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define CONFIG_BRIDGE_PHONE 0
using esp_err_t=int; using esp_bd_addr_t=uint8_t[6]; using nvs_handle_t=int;
constexpr int ESP_OK=0, ESP_ERR_NVS_NOT_FOUND=1, NVS_READONLY=0, NVS_READWRITE=1;
struct State { bool linked=false; } self;
bool dirty=false, connecting=false, peer_saved=false, classic_base=false;
unsigned reconnect_attempts=0, reconnect_delay=5000, audio_rate=0;
int last_connect_result=ESP_OK;
int64_t tick=0,next_connect=5000;
esp_bd_addr_t peer{}, saved{}, bonded_address{1,2,3,4,5,6};
bool stored=false,bonded=true;
size_t stored_size=6;
int opens=0,closes=0;
int64_t now(){return tick;}
void call_audio_set(int,int,unsigned){}
void classic_base_link_changed(bool connected){classic_base=connected;}
int esp_bt_gap_get_bond_device_num(){return bonded?1:0;}
int esp_bt_gap_get_bond_device_list(int*count,esp_bd_addr_t*out){assert(*count==1);memcpy(out[0],bonded_address,6);return 0;}
bool classic_bond_known(const uint8_t*address){
 int count=esp_bt_gap_get_bond_device_num(); esp_bd_addr_t bonds[16];
 if(count<1||count>16||esp_bt_gap_get_bond_device_list(&count,bonds)!=ESP_OK)return false;
 for(int index=0;index<count;++index)if(!memcmp(bonds[index],address,6))return true;
 return false;
}
int nvs_open(const char*,int mode,int*h){*h=1;return (mode==NVS_READONLY&&!stored)?ESP_ERR_NVS_NOT_FOUND:ESP_OK;}
int nvs_set_blob(int,const char*,const void*p,size_t n){assert(n==6);memcpy(saved,p,n);stored=true;stored_size=n;return 0;}
int nvs_get_blob(int,const char*,void*p,size_t*n){if(!stored)return ESP_ERR_NVS_NOT_FOUND;assert(*n==6);memcpy(p,saved,stored_size);*n=stored_size;return 0;}
int nvs_commit(int){return 0;}
void nvs_close(int){}
int esp_hf_ag_slc_connect(uint8_t*p){assert(!memcmp(p,bonded_address,6));++opens;return ESP_OK;}
int esp_hf_ag_slc_disconnect(uint8_t*){++closes;return ESP_OK;}
'''
for signature in ['static void save_peer(', 'static void load_peer(', 'static void set_connected(']:
    harness += function(signature) + '\n'
harness += 'void poll(){\n'+poll+'\n}\n'
harness += r'''
void restart(){
 self={};connecting=false;peer_saved=false;classic_base=false;memset(peer,0,6);
 reconnect_attempts=0;reconnect_delay=5000;last_connect_result=0;
 tick=0;next_connect=5000;opens=closes=0;load_peer();
}
int main(){
 // Real save/load helpers, backed by persistent fake NVS and bond storage.
 set_connected(true,bonded_address);assert(stored&&peer_saved&&self.linked&&classic_base);
 restart();assert(peer_saved&&!self.linked&&!memcmp(peer,bonded_address,6));
 tick=5000;poll();tick=25000;poll();assert(opens==0&&closes==0&&!connecting);
 // Stale rejection callbacks must not schedule another outgoing
 // gateway connection; Tesla owns the successful reconnect direction.
 restart();
'''
for event in fixture['events']:
    if event['event'] == 'disconnected':
        harness += f'''tick={event['ms']};set_connected(false,bonded_address);
 assert(!connecting&&!self.linked&&!classic_base&&peer_saved&&bonded&&stored);
 tick+=60000;poll();assert(opens==0&&closes==0);
'''
    else:
        harness += f'''tick={event['ms']};poll();
 assert(!connecting&&reconnect_attempts==0&&opens==0);
'''
harness += r'''
 // A later Tesla-initiated connection retains the peer without outgoing work.
 tick=100000;set_connected(true,bonded_address);
 tick=200000;poll();assert(opens==0&&closes==0&&self.linked&&stored);
 // Another reboot restores the pairing and listens without initiating.
 restart();assert(peer_saved);tick=5000;poll();assert(opens==0);
 // Reject stale storage when the actual Bluetooth bond has been removed.
 bonded=false;restart();assert(!peer_saved);tick=90000;poll();assert(opens==0);
 bonded=true;stored_size=5;restart();assert(!peer_saved);
 stored=false;restart();assert(!peer_saved);
 puts("PASS production peer persistence, passive Tesla reconnect policy, inbound success and absent-bond rejection");
 puts("LIMIT: controller callbacks are replayed inputs; hardware verifies Tesla's connection direction.");
}
'''
with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary)
    (path/'test.cpp').write_text(harness)
    subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
