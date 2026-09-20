"""Replay Tesla SDP attribute queries against original and patched SDK parsers."""
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
SDK = Path(os.environ['IDF_PATH'])
STACK = SDK / 'components/bt/host/bluedroid/stack'
spec = importlib.util.spec_from_file_location('sdp_patch', ROOT / 'firmware/sdp_diagnostics/patch.py')
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


def function(source):
    start = source.index('UINT8 *sdpu_extract_attr_seq (')
    return source[start:source.index('\n}\n', start) + 3]


def parser_namespace(name, header, source):
    limit = re.search(r'#define\s+MAX_ATTR_PER_SEQ\s+(\d+)', header).group(1)
    structs = header.split('/* Internal attribute sequence definitions */', 1)[1]
    structs = structs[:structs.index('} tSDP_ATTR_SEQ;') + len('} tSDP_ATTR_SEQ;')]
    return f'namespace {name} {{\nconstexpr unsigned MAX_ATTR_PER_SEQ = {limit};\n' + structs + function(source) + '\n}\n'


macros = '\n'.join(line for line in (STACK / 'include/stack/bt_types.h').read_text().splitlines()
                   if re.match(r'#define BE_STREAM_TO_UINT(8|16|32)\(', line))
constants = '\n'.join(line for line in (STACK / 'include/stack/sdpdefs.h').read_text().splitlines()
                      if re.match(r'#define\s+(UINT_DESC_TYPE|DATA_ELE_SEQ_DESC_TYPE|SIZE_\w+)\s', line))
with tempfile.TemporaryDirectory(prefix='dashbridge-attributes-') as temp:
    temp = Path(temp)
    patch.prepare(SDK / 'components/bt', temp)
    harness = '#include <cstdint>\n#include <cassert>\n#include <vector>\n#include <string>\n'
    harness += 'using UINT8=uint8_t; using UINT16=uint16_t; using UINT32=uint32_t;\n'
    harness += macros + '\n' + constants + '\n'
    harness += parser_namespace('original', (STACK / 'sdp/include/sdpint.h').read_text(),
                                (STACK / 'sdp/sdp_utils.c').read_text())
    harness += parser_namespace('fixed', (temp / 'sdpint.h').read_text(), (temp / 'sdp_utils.c').read_text())
    harness += r'''
std::vector<UINT8> decode(const std::string &hex) {
    std::vector<UINT8> bytes;
    for (size_t i=0;i<hex.size();i+=2) bytes.push_back(std::stoul(hex.substr(i,2),nullptr,16));
    return bytes;
}
void replay(const char *hex, const std::vector<UINT16> &expected, bool original_accepts) {
    auto request=decode(hex);
    // These captured PDUs contain a 5-byte header, 7-byte UUID sequence and
    // 2-byte maximum response length; the attribute sequence starts at 14.
    auto p=request.data()+14;
    original::tSDP_ATTR_SEQ old{};
    fixed::tSDP_ATTR_SEQ current{};
    auto old_end=original::sdpu_extract_attr_seq(p,request.size()-14,&old);
    assert((old_end != nullptr) == original_accepts);
    auto end=fixed::sdpu_extract_attr_seq(p,request.size()-14,&current);
    assert(end==request.data()+request.size()-1 && *end==0);
    assert(current.num_attr==expected.size());
    for (size_t i=0;i<expected.size();++i) {
        assert(current.attr_entry[i].start==expected[i]);
        assert(current.attr_entry[i].end==expected[i]);
    }
}
int main() {
    replay("060001002135051a0000111f0296351509000009000109000409000909010009030109031100",
           {0,1,4,9,0x100,0x301,0x311},true);
    replay("060001002435051a000011320296351809000009000109000409010009020009031509031609031700",
           {0,1,4,0x100,0x200,0x315,0x316,0x317},true);
    replay("060001002a35051a000012000296351e09000109000a09000b09010109020009020109020209020309020409020500",
           {1,0xa,0xb,0x101,0x200,0x201,0x202,0x203,0x204,0x205},false);
    // Exercise the new capacity, one over capacity, and ranged attributes.
    for (unsigned count : {1u,7u,8u,10u,15u,16u,17u,32u}) {
        std::vector<UINT8> list{0x35,static_cast<UINT8>(count*3)};
        for (unsigned i=0;i<count;++i) list.insert(list.end(),{0x09,0x00,static_cast<UINT8>(i)});
        fixed::tSDP_ATTR_SEQ parsed{};
        auto end=fixed::sdpu_extract_attr_seq(list.data(),list.size(),&parsed);
        if (count<=16) {
            assert(end==list.data()+list.size()); assert(parsed.num_attr==count);
        } else { assert(end==nullptr); assert(parsed.num_attr==16); }
    }
    auto range=decode("35050a0000ffff");
    fixed::tSDP_ATTR_SEQ parsed{};
    assert(fixed::sdpu_extract_attr_seq(range.data(),range.size(),&parsed)==range.data()+range.size());
    assert(parsed.num_attr==1 && parsed.attr_entry[0].start==0 && parsed.attr_entry[0].end==0xffff);
}
'''
    source = temp / 'check.cpp'
    binary = temp / 'check'
    source.write_text(harness)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('PASS captured Tesla HFP/MAP/Device ID queries, exact capacity, overflow rejection and ranges')
