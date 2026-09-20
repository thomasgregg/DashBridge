"""Replay the real upstream and patched SSP callbacks with a stalled name lookup."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
bt = Path(os.environ['IDF_PATH']) / 'components/bt'
spec = importlib.util.spec_from_file_location('sdp_patch', root / 'firmware/sdp_diagnostics/patch.py')
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)

def callback(text):
    start = text.index('static UINT8 bta_dm_sp_cback (tBTM_SP_EVT event, tBTM_SP_EVT_DATA *p_data)\n{')
    return text[start:text.index('\n}\n', start) + 3]

stubs = r'''
using UINT8=uint8_t; using tBTM_STATUS=int; using tBTM_SP_EVT=int; using tBTA_DM_SEC_EVT=int;
constexpr int BD_NAME_LEN=248, BTM_CMD_STARTED=1, BTM_NOT_AUTHORIZED=2, BTM_SUCCESS=0, BT_TRANSPORT_BR_EDR=0;
enum { BTM_SP_IO_REQ_EVT, BTM_SP_IO_RSP_EVT, BTM_SP_CFM_REQ_EVT, BTM_SP_KEY_REQ_EVT,
       BTM_SP_KEY_NOTIF_EVT, BTM_SP_COMPLT_EVT, BTM_SP_KEYPRESS_EVT, BTM_SP_UPGRADE_EVT,
       BTA_DM_SP_KEY_NOTIF_EVT, BTA_DM_SP_CFM_REQ_EVT, BTA_DM_SP_KEY_REQ_EVT, BTA_DM_SP_KEYPRESS_EVT };
#define FALSE 0
#define TRUE 1
#define BTM_OOB_INCLUDED FALSE
#define APPL_TRACE_EVENT(...) ((void)0)
#define APPL_TRACE_WARNING(...) ((void)0)
#define bdcpy(a,b) memcpy(a,b,6)
#define BTA_COPY_DEVICE_CLASS(a,b) memcpy(a,b,3)
#define BCM_STRNCPY_S(a,b,n) strncpy(a,b,n)
struct Event {
 uint8_t bd_addr[6],dev_class[3],bd_name[BD_NAME_LEN+1];
 union { uint32_t num_val; uint32_t passkey; };
 bool just_works; uint8_t loc_auth_req,rmt_auth_req,loc_io_caps,rmt_io_caps;
};
using tBTM_SP_KEYPRESS=uint32_t;
union tBTA_DM_SEC { Event cfm_req,key_notif; tBTM_SP_KEYPRESS key_press; };
union tBTM_SP_EVT_DATA {
 Event cfm_req,key_notif;
 struct {uint8_t bd_addr[6],io_cap,oob_data,auth_req,is_orig;} io_req,io_rsp;
 struct {uint8_t bd_addr[6]; bool upgrade;} upgrade;
 tBTM_SP_KEYPRESS key_press;
};
struct State {
 void (*p_sec_cback)(int,tBTA_DM_SEC*); bool just_works;
 uint32_t num_val; int pin_evt; uint8_t pin_bd_addr[6],pin_dev_class[3];
} bta_dm_cb{};
int lookups=0, deliveries=0, last_event=-1;
tBTA_DM_SEC delivered{};
void receiver(int event,tBTA_DM_SEC* data) { ++deliveries; last_event=event; delivered=*data; }
void bta_dm_pinname_cback(void*) {}
int BTM_ReadRemoteDeviceName(uint8_t*,void(*)(void*),int) { ++lookups; return BTM_CMD_STARTED; }
void bta_dm_co_io_req(...) {}
void bta_dm_co_io_rsp(...) {}
void bta_dm_co_lk_upgrade(...) {}
'''
with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary)
    patch.prepare(bt, path)
    original = (bt / 'host/bluedroid/bta/dm/bta_dm_act.c').read_text()
    fixed = (path / 'bta_dm_act.c').read_text()
    assert original.replace(callback(original), callback(fixed)) == fixed
    harness = '#include <cassert>\n#include <cstdint>\n#include <cstring>\n'
    for name, text in [('original', original), ('fixed', fixed)]:
        harness += f'namespace {name} {{\n' + stubs + callback(text) + '\n}\n'
    harness += r'''
int main() {
 original::bta_dm_cb.p_sec_cback=original::receiver;
 original::tBTM_SP_EVT_DATA before{};
 before.cfm_req.just_works=true;
 assert(original::bta_dm_sp_cback(original::BTM_SP_CFM_REQ_EVT,&before)==original::BTM_CMD_STARTED);
 assert(original::lookups==1 && original::deliveries==0); // reproduces the log's stall
 for (bool just_works : {false,true}) {
  for (int name_length : {0,5,fixed::BD_NAME_LEN}) {
   fixed::lookups=fixed::deliveries=0;
   fixed::bta_dm_cb.p_sec_cback=fixed::receiver;
   fixed::tBTM_SP_EVT_DATA request{};
   auto &in=request.cfm_req;
   const uint8_t addr[]={1,2,3,4,5,6}, cls[]={8,4,44};
   memcpy(in.bd_addr,addr,6);memcpy(in.dev_class,cls,3);
   memset(in.bd_name,'x',name_length);in.num_val=123456;in.just_works=just_works;
   in.loc_auth_req=3;in.rmt_auth_req=2;in.loc_io_caps=3;in.rmt_io_caps=1;
   auto untouched=request;
   assert(fixed::bta_dm_sp_cback(fixed::BTM_SP_CFM_REQ_EVT,&request)==fixed::BTM_CMD_STARTED);
   assert(fixed::lookups==0 && fixed::deliveries==1);
   assert(fixed::last_event==fixed::BTA_DM_SP_CFM_REQ_EVT);
   const auto &out=fixed::delivered.cfm_req;
   assert(!memcmp(out.bd_addr,addr,6) && !memcmp(out.dev_class,cls,3));
   assert(!memcmp(out.bd_name,in.bd_name,sizeof in.bd_name));
   assert(out.num_val==123456 && out.just_works==just_works);
   assert(out.loc_auth_req==3 && out.rmt_auth_req==2 && out.loc_io_caps==3 && out.rmt_io_caps==1);
   assert(!memcmp(&request,&untouched,sizeof request));
   fixed::bta_dm_cb.p_sec_cback=nullptr;
   assert(fixed::bta_dm_sp_cback(fixed::BTM_SP_CFM_REQ_EVT,&request)==fixed::BTM_NOT_AUTHORIZED);
   assert(fixed::deliveries==1); // never grants pairing without the security callback
  }
 }
 fixed::bta_dm_cb.p_sec_cback=fixed::receiver;
 fixed::lookups=fixed::deliveries=0;
 fixed::tBTM_SP_EVT_DATA other{};
 fixed::bta_dm_sp_cback(fixed::BTM_SP_KEY_NOTIF_EVT,&other);
 assert(fixed::lookups==1 && fixed::deliveries==0); // unrelated passkey flow unchanged
}
'''
    harness = '#include <initializer_list>\n' + harness
    (path / 'test.cpp').write_text(harness)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('PASS pairing: stalled-name regression, immediate SSP dispatch, intact security fields, missing callback rejection, other events unchanged')
