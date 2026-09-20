"""Replay failed feature reads against the pinned SDK and patched callback."""
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


def callback(source):
    start = source.index('void btm_read_remote_ext_features_failed (')
    return source[start:source.index('\n}\n', start) + 3]


with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary)
    patch.prepare(bt, path)
    original = (bt / 'host/bluedroid/stack/btm/btm_acl.c').read_text()
    fixed = (path / 'btm_acl.c').read_text()
    # The only code change must be the guard in the failed-query callback.
    assert original.replace(callback(original), callback(fixed)) == fixed
    harness = '#include <cassert>\n#include <cstdint>\n#include <cstddef>\n'
    stubs = r'''
using UINT8=uint8_t; using UINT16=uint16_t;
enum { HCI_ERR_NO_CONNECTION=0x02 };
struct tACL_CONN {} connection;
bool live = true;
int processed=0, established=0;
void log(const char*, ...) {}
#define BTM_TRACE_WARNING(...) log(__VA_ARGS__)
#define BTM_TRACE_ERROR(...) log(__VA_ARGS__)
tACL_CONN* btm_handle_to_acl(UINT16) { return live ? &connection : nullptr; }
void btm_process_remote_ext_features(tACL_CONN*, UINT8) { ++processed; }
void btm_establish_continue(tACL_CONN*) { ++established; }
'''
    for name, source in [('original', original), ('fixed', fixed)]:
        harness += f'namespace {name} {{\n' + stubs + callback(source) + '\n}\n'
    harness += r'''
int main() {
    original::btm_read_remote_ext_features_failed(0x02, 0x81);
    assert(original::processed == 1 && original::established == 1);
    fixed::btm_read_remote_ext_features_failed(0x02, 0x81);
    assert(fixed::processed == 0 && fixed::established == 0);
    // Repeated late events must not change capabilities or accept the link.
    fixed::btm_read_remote_ext_features_failed(0x02, 0x81);
    assert(fixed::processed == 0 && fixed::established == 0);
    fixed::live = false;
    fixed::btm_read_remote_ext_features_failed(0x02, 0x81);
    fixed::btm_read_remote_ext_features_failed(0x1a, 0x81);
    assert(fixed::processed == 0 && fixed::established == 0);
    fixed::live = true;
    fixed::btm_read_remote_ext_features_failed(0x1a, 0x81);
    assert(fixed::processed == 1 && fixed::established == 1);
}
'''
    (path / 'test.cpp').write_text(harness)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('PASS closed-link feature failure: no capability update or connection acceptance; other paths unchanged')
