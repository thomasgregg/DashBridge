"""Exercise the production packet tracer with exact-size and truncated buffers."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('sdp_patch', root / 'firmware/sdp_diagnostics/patch.py')
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)
harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef struct { UINT16 event, len, offset, layer_specific; } BT_HDR;
static char captured[512];
#define SDP_TRACE_WARNING(...) snprintf(captured, sizeof captured, __VA_ARGS__)
TRACE_CODE
int main(void) {
    const unsigned sizes[] = {0, 1, 191, 192, 193, 1024};
    for (unsigned s = 0; s < sizeof sizes / sizeof sizes[0]; ++s) {
        for (unsigned offset = 0; offset <= 7; offset += 7) {
            unsigned length = sizes[s];
            size_t bytes = sizeof(BT_HDR) + offset + length;
            BT_HDR *msg = malloc(bytes);
            assert(msg);
            memset(msg, 0xa5, bytes);
            msg->len = length; msg->offset = offset;
            UINT8 *payload = (UINT8 *)(msg + 1) + offset;
            for (unsigned i = 0; i < length; ++i) payload[i] = i;
            void *original = malloc(bytes); assert(original);
            memcpy(original, msg, bytes);
            dashbridge_sdp_trace("client RX", 0x42, msg);
            assert(memcmp(original, msg, bytes) == 0);
            unsigned limit = length < 192 ? length : 192;
            char expected[512];
            int n = snprintf(expected, sizeof expected,
                "DashBridge SDP client RX cid=0x42 bytes=%u captured=%u data=", length, limit);
            for (unsigned i = 0; i < limit; ++i)
                n += snprintf(expected + n, sizeof expected - n, "%02x", payload[i]);
            snprintf(expected + n, sizeof expected - n, "\n");
            assert(strcmp(captured, expected) == 0);
            free(original); free(msg);
        }
    }
}
'''.replace('TRACE_CODE', patch.TRACE_HEADER)
with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary)
    (path / 'test.c').write_text(harness)
    subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('PASS SDP trace: exact bytes, offsets, empty packets, truncation, unchanged input')
