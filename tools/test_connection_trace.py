"""Exercise the production handshake tracer: bounds, privacy, and useful fields."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char captured[2048];
static unsigned lines;
#define DB_TRACE(...) do { ++lines; snprintf(captured, sizeof captured, __VA_ARGS__); } while (0)
#include "connection_trace.h"
static void clear(void) { captured[0]=0; lines=0; }
static void put16(uint8_t *p, unsigned n) { p[0]=n; p[1]=n>>8; }
int main(void) {
    /* Exact allocations catch overreads, including malformed header lengths. */
    for (unsigned n=0; n<=260; ++n) {
        uint8_t *p=malloc(n ? n : 1), *copy=malloc(n ? n : 1);
        assert(p && copy);
        for (unsigned code=0; code<256; ++code) {
            memset(p, 0xa5, n);
            if (n) p[0]=code;
            if (n>1) p[1]=255;
            memcpy(copy,p,n);
            db_hci_event(p,n);
            db_hci_command(p,n,-1);
            db_hci_command(p,n,2);
            db_l2cap_signal("RX",p,n);
            assert(!memcmp(copy,p,n));
        }
        free(p); free(copy);
    }
    /* All opcode paths, including invalid and vendor opcodes. */
    uint8_t command[258]; memset(command,0xa5,sizeof command); command[2]=255;
    for (unsigned op=0; op<65536; ++op) {
        put16(command,op); db_hci_command(command,sizeof command,-1);
    }
    /* Secret command parameters never enter the log. */
    const unsigned sensitive[]={0x040b,0x040d,0x0c11,0x042e,0x0430,0x0433};
    for (unsigned i=0;i<sizeof sensitive/sizeof sensitive[0];++i) {
        put16(command,sensitive[i]); db_hci_command(command,sizeof command,-1);
        char before[2048]; strcpy(before,captured);
        memset(command+3,0x5a,sizeof command-3);
        db_hci_command(command,sizeof command,-1);
        assert(!strcmp(before,captured));
        assert(strstr(captured,"captured=0 data="));
    }
    /* Event-specific truncation removes keys, passkeys, and remote names. */
    const unsigned events[]={0x18,0x33,0x3b,0x07};
    for (unsigned i=0;i<sizeof events/sizeof events[0];++i) {
        uint8_t event[257]; memset(event,0xa5,sizeof event); event[0]=events[i]; event[1]=255;
        db_hci_event(event,sizeof event); char before[2048]; strcpy(before,captured);
        unsigned safe=events[i]==7 ? 7 : 6;
        memset(event+2+safe,0x5a,sizeof event-2-safe);
        db_hci_event(event,sizeof event); assert(!strcmp(before,captured));
    }
    uint8_t auth[]={6,3,5,0x81,0}; clear(); db_hci_event(auth,sizeof auth);
    assert(lines==1 && strstr(captured,"authentication-complete") && strstr(captured,"data=058100"));
    uint8_t disc[]={6,4,0x81,0,0x13,0}; /* command: Disconnect */
    disc[2]=3; disc[3]=0x81; disc[4]=0; disc[5]=0x13;
    clear(); db_hci_command(disc,sizeof disc,-1);
    assert(lines==1 && strstr(captured,"op=0x0406") && strstr(captured,"data=810013"));
    /* Command completion returns only status, not any returned key material. */
    uint8_t complete[]={0x0e,8,1,0x11,0x0c,0,0xa5,0xa5,0xa5,0xa5};
    db_hci_event(complete,sizeof complete); assert(strstr(captured,"captured=4 data=01110c00"));
    uint8_t signal[]={0x81,0x20,12,0,8,0,1,0,3,1,4,0,0x40,0,2,0};
    clear(); db_l2cap_signal("RX",signal,sizeof signal);
    assert(lines==1 && strstr(captured,"code=0x03") && strstr(captured,"data=40000200"));
    /* Dynamic payload and security channels, and continuations, stay silent. */
    for (unsigned cid=2;cid<256;++cid) {
        signal[6]=cid; clear(); db_l2cap_signal("RX",signal,sizeof signal); assert(!lines);
    }
    signal[6]=1; signal[1]=0x10; clear(); db_l2cap_signal("RX",signal,sizeof signal); assert(!lines);
    signal[1]=0x20;
    for (unsigned code=8;code<=9;++code) {
        signal[8]=code; clear(); db_l2cap_signal("TX-queued",signal,sizeof signal);
        assert(lines==1 && strstr(captured,"captured=0 data="));
    }
    /* Exhaust malformed signal length combinations with exact-size buffers. */
    for (unsigned n=8;n<96;++n) {
        uint8_t *p=calloc(1,n); assert(p); p[1]=0x20; p[6]=1; p[8<n?8:0]=3;
        for (unsigned len=0;len<128;++len) {
            put16(p+2,len); put16(p+4,len);
            if(n>=12) put16(p+10,len);
            db_l2cap_signal("RX",p,n);
        }
        free(p);
    }
    /* Two commands in one packet must both be recorded. */
    uint8_t multi[]={0x81,0x20,12,0,8,0,1,0,6,1,0,0,7,2,0,0};
    clear(); db_l2cap_signal("RX",multi,sizeof multi); assert(lines==2);
}
'''
with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary)
    (path / 'test.c').write_text(harness)
    subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(root / 'firmware/sdp_diagnostics'),
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('PASS connection trace: bounds, all opcodes, secret redaction, auth/disconnect, signaling, unchanged buffers')
