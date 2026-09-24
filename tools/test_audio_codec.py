"""Replay real IDF HFP receive callbacks with the real mSBC decoder and PLC."""
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
BT = Path(os.environ['IDF_PATH']) / 'components/bt'
SBC = BT / 'host/bluedroid/external/sbc'
spec = importlib.util.spec_from_file_location('codec_patch', ROOT / 'firmware/audio_codec/patch.py')
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


def byte_array(text, marker):
    start = text.index(marker)
    body = text[start:text.index('};', start)]
    return bytes(int(value, 16) for value in re.findall(r'0x([0-9a-fA-F]{2})', body))


protocol = (ROOT / 'firmware/protocols/calls_v3/include/dashbridge/protocols/calls_v3.hpp').read_text()
plc_source = (SBC / 'plc/sbc_plc.c').read_text()
firmware_silence = byte_array(protocol, 'msbc_silence_frame = {')
sdk_silence = byte_array(plc_source, 'static const uint8_t indices0[] = {')
assert len(firmware_silence) == 57 and firmware_silence == sdk_silence
print('PASS encoded concealment frame matches the pinned SDK mSBC zero-signal frame')


def function(text, marker):
    start = text.index(marker)
    return text[start:text.index('\n}\n', start) + 3]


with tempfile.TemporaryDirectory(prefix='dashbridge-codec-') as temporary:
    temp = Path(temporary)
    patch.prepare(BT, temp / 'patched')
    shutil.copytree(SBC / 'decoder/include', temp / 'include')
    # The embedded SDK uses 32-bit long. Preserve its type widths on a 64-bit host.
    cpu = temp / 'include/oi_cpu_dep.h'
    cpu.write_text(cpu.read_text().replace('signed long ', 'signed int ').replace('unsigned long ', 'unsigned int '))
    (temp / 'include/common').mkdir()
    (temp / 'include/common/bt_target.h').write_text('''#include <stddef.h>
#include <string.h>
#define TRUE 1
#define FALSE 0
#define SBC_DEC_INCLUDED TRUE
#define PLC_INCLUDED TRUE
''')
    flags = ['-std=c11', '-g', '-O1', '-fsanitize=address', '-fno-omit-frame-pointer',
             '-I' + str(temp / 'include'), '-I' + str(SBC / 'plc/include')]
    cc = os.environ.get('CC', 'clang')
    objects = []
    for source in [*sorted((SBC / 'decoder/srce').glob('*.c')), SBC / 'plc/sbc_plc.c']:
        obj = temp / (source.stem + '.o')
        subprocess.run([cc, *flags, '-c', str(source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    for role, name in [('ag', 'hf_ag/bta_ag_co.c'), ('hf_client', 'hf_client/bta_hf_client_co.c')]:
        prefix = 'bta_' + role
        original = (BT / 'host/bluedroid/btc/profile/std' / name).read_text()
        fixed = (temp / 'patched' / Path(name).name).read_text()
        for variant, text in [('original', original), ('fixed', fixed)]:
            # Production packet parser and decode/PLC function, not a duplicate implementation.
            functions = function(text, 'static void ' + prefix + '_decode_msbc_frame(')
            functions += function(text, 'void ' + prefix + '_sco_co_in_data(')
            callback = 'btc_hf_incoming_data_cb_to_app' if role == 'ag' else 'btc_hf_client_incoming_data_cb_to_app'
            harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "common/bt_target.h"
#include "oi_codec_sbc.h"
#include "oi_status.h"
#include "sbc_plc.h"
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef int BOOLEAN;
typedef int tBTM_SCO_DATA_FLAG;
#define BTC_HFP_EXT_CODEC FALSE
#define BTM_MSBC_FRAME_SIZE 60
#define BTM_MSBC_FRAME_DATA_SIZE 57
#define HF_SBC_DEC_RAW_DATA_SIZE 240
#define HF_SBC_DEC_CONTEXT_DATA_LEN CODEC_DATA_WORDS(1, SBC_CODEC_FAST_FILTER_BUFFERS)
#define BTM_SCO_DATA_CORRECT 0
#define BTM_SCO_AIR_MODE_CVSD 1
#define BTM_SCO_AIR_MODE_TRANSPNT 3
#define STREAM_SKIP_UINT16(p) ((p) += 2)
#define STREAM_TO_UINT8(v,p) ((v) = *(p)++)
#define APPL_TRACE_DEBUG(...) ((void)0)
#define APPL_TRACE_ERROR(...) ((void)0)
#define osi_free free
_Static_assert(sizeof(OI_UINT32) == 4, "match ESP32 ABI");
typedef struct { uint16_t len, offset; } BT_HDR;
static struct {
 OI_CODEC_SBC_DECODER_CONTEXT decoder_context;
 OI_UINT32 decoder_context_data[HF_SBC_DEC_CONTEXT_DATA_LEN];
 OI_INT16 decode_raw_data[HF_SBC_DEC_RAW_DATA_SIZE];
 int is_bad_frame, decode_first_pkt;
 uint8_t decode_msbc_data[60];
} PREFIX_co_cb;
static struct {
 int first_good_frame_found;
 sbc_plc_state_t plc_state;
 int16_t sbc_plc_out[SBC_FS];
} bta_hf_ct_plc;
static UINT8 hf_air_mode=BTM_SCO_AIR_MODE_TRANSPNT, hf_inout_pkt_size=60;
static unsigned delivered;
static void CALLBACK(const uint8_t *pcm, uint32_t size) {
 assert(size==240 || hf_air_mode==BTM_SCO_AIR_MODE_CVSD);
 for (unsigned i=0;i<size;++i) { volatile uint8_t byte=pcm[i]; (void)byte; }
 ++delivered;
}
'''.replace('PREFIX', prefix).replace('CALLBACK', callback)
            harness += functions
            harness += r'''
static void reset(unsigned packet_size) {
 memset(&PREFIX_co_cb,0,sizeof PREFIX_co_cb);
 memset(&bta_hf_ct_plc,0,sizeof bta_hf_ct_plc);
 hf_air_mode=BTM_SCO_AIR_MODE_TRANSPNT;
 hf_inout_pkt_size=packet_size;
 PREFIX_co_cb.decode_first_pkt=1;
 assert(OI_CODEC_SBC_DecoderReset(&PREFIX_co_cb.decoder_context,
   PREFIX_co_cb.decoder_context_data, sizeof PREFIX_co_cb.decoder_context_data,
   1,1,FALSE,TRUE)==OI_OK);
 sbc_plc_init(&bta_hf_ct_plc.plc_state);
 delivered=0;
}
static void receive(const uint8_t *data, unsigned actual, unsigned declared, int status) {
 BT_HDR *packet=calloc(1,sizeof(BT_HDR)+3+actual);
 assert(packet);
 packet->len=3+actual;
 uint8_t *p=(uint8_t *)(packet+1);
 p[2]=declared; memcpy(p+3,data,actual);
 PREFIX_sco_co_in_data(packet,status);
}
int main(int argc, char **argv) {
 uint8_t frame[60]={1,8};
 memcpy(frame+2,sbc_plc_zero_signal_frame(),57);
 if (argc>1 && strcmp(argv[1],"plc")==0) {
  reset(60);
  bta_hf_ct_plc.first_good_frame_found=TRUE;
  receive(frame,60,60,1); // Exercises the independent one-byte PLC length bug.
  return 0;
 }
 reset(60);
 receive(frame,60,60,0); // Unmodified SDK must fail ASan here.
 assert(delivered==1);
 if (argc>1) return 0;
 for (unsigned i=0;i<1000;++i) receive(frame,60,60,0);
 assert(delivered==1001);
 receive(frame,60,60,1); // Corrupt/lost radio packet: PLC emits exactly one frame.
 assert(delivered==1002);
 frame[5]^=1; receive(frame,60,60,0); frame[5]^=1; // Bad codec checksum.
 assert(delivered==1003);
 receive(frame,60,60,0); assert(delivered==1004);
 for (unsigned n=0;n<60;++n) receive(frame,n,60,0); // Truncated HCI payload.
 BT_HDR *short_packet=calloc(1,sizeof(BT_HDR)+2);
 short_packet->len=2; PREFIX_sco_co_in_data(short_packet,0);
 sbc_plc_deinit(&bta_hf_ct_plc.plc_state);
 reset(30);
 for (unsigned i=0;i<1000;++i) {
  receive(frame,30,30,0);
  assert(delivered==i);
  receive(frame+30,30,30,0);
  assert(delivered==i+1);
 }
 receive(frame,30,30,1); receive(frame+30,30,30,0);
 assert(delivered==1001);
 receive(frame,30,30,0); receive(frame+30,30,30,0);
 assert(delivered==1002);
 sbc_plc_deinit(&bta_hf_ct_plc.plc_state);
 hf_air_mode=BTM_SCO_AIR_MODE_CVSD;
 receive(frame,30,30,0); assert(delivered==1003);
 return 0;
}
'''.replace('PREFIX', prefix)
            source = temp / f'{role}-{variant}.c'
            source.write_text(harness)
            binary = source.with_suffix('')
            subprocess.run([cc, *flags, str(source), *objects, '-lm', '-o', str(binary)], check=True)
            if variant == 'original':
                for scenario in ['good', 'plc']:
                    result = subprocess.run([str(binary), scenario], text=True, capture_output=True)
                    assert result.returncode != 0 and 'stack-buffer-overflow' in result.stderr, result.stderr
                    print(f'REPRODUCED {role}: upstream {scenario} decoder length stack overflow')
            else:
                subprocess.run([str(binary)], check=True)
                print(f'PASS {role}: complete/split frames, PLC, checksum failure/recovery, truncated packets, CVSD; ASan clean')
