"""Correct pinned IDF HFP receive lengths in build-local copies, leaving SDK intact."""
import hashlib
from pathlib import Path
import sys

SOURCES = {
    'hf_ag/bta_ag_co.c': 'cf93030233c240c4c4956f78fa1725edd1fc773c7eb9caa07c2493329d702aa3',
    'hf_client/bta_hf_client_co.c': '7704224756b690ee0b9f25f227c2afa299d9f5ea63de3f4b7bb7b5c41e14a165',
}


def once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError(f'Review HFP decoder patch marker: {old}')
    return text.replace(old, new)


def prepare(bt_dir, output):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    for name, digest in SOURCES.items():
        original = (Path(bt_dir) / 'host/bluedroid/btc/profile/std' / name).read_bytes()
        if hashlib.sha256(original).hexdigest() != digest:
            raise RuntimeError(f'Review HFP decoder patch for changed SDK: {name}')
        text = original.decode()
        prefix = 'bta_ag' if name.startswith('hf_ag/') else 'bta_hf_client'
        # DecodeFrame mutates a UINT32 byte count; a cast of UINT8* is invalid.
        text = once(text, 'UINT8 **data, UINT8 *length, BOOLEAN is_bad_frame',
                    'const UINT8 *data, OI_UINT32 length, BOOLEAN is_bad_frame')
        text = once(text, 'UINT8 zero_signal_frame_len = BTM_MSBC_FRAME_DATA_SIZE;',
                    'OI_UINT32 zero_signal_frame_len = BTM_MSBC_FRAME_DATA_SIZE;')
        text = once(text, '    OI_STATUS status;\n    const OI_BYTE *zero_signal_frame_data;',
                    '    OI_STATUS status;\n'
                    '    /* The SBC bit reader prefetches up to three bytes past the frame. */\n'
                    '    OI_BYTE padded_frame[BTM_MSBC_FRAME_SIZE + 4] = {0};\n'
                    '    const OI_BYTE *frame_data = padded_frame;\n'
                    '    if (length > BTM_MSBC_FRAME_SIZE) is_bad_frame = TRUE;\n'
                    '    if (!is_bad_frame) memcpy(padded_frame, data, length);\n'
                    '    const OI_BYTE *zero_signal_frame_data;')
        text = once(text, '(const OI_BYTE **)data,', '&frame_data,')
        text = once(text, '            zero_signal_frame_data = sbc_plc_zero_signal_frame();',
                    '            memset(padded_frame, 0, sizeof(padded_frame));\n'
                    '            memcpy(padded_frame, sbc_plc_zero_signal_frame(), BTM_MSBC_FRAME_DATA_SIZE);\n'
                    '            zero_signal_frame_data = padded_frame;')
        text = once(text, '(OI_UINT32 *)length,', '&length,')
        text = once(text, '(OI_UINT32 *)&zero_signal_frame_len,', '&zero_signal_frame_len,')
        call = f'{prefix}_decode_msbc_frame(&data, &pkt_size, {prefix}_co_cb.is_bad_frame);'
        if text.count(call) != 2:
            raise RuntimeError('Expected split and complete mSBC receive paths')
        # The two 30-byte HCI payloads have now formed one complete 60-byte frame.
        text = text.replace(call, f'{prefix}_decode_msbc_frame(data, BTM_MSBC_FRAME_SIZE, {prefix}_co_cb.is_bad_frame);', 1)
        text = text.replace(call, f'{prefix}_decode_msbc_frame(data, pkt_size, {prefix}_co_cb.is_bad_frame);', 1)
        # Do not read an absent HCI header or bytes beyond the allocation.
        marker = '    UINT8 *p = (UINT8 *)(p_buf + 1) + p_buf->offset;'
        text = once(text, marker,
                    '    if (p_buf->len < 3) { osi_free(p_buf); return; }\n' + marker)
        if name.startswith('hf_client/'):
            marker = '    STREAM_TO_UINT8 (pkt_size, p);'
            text = once(text, marker, marker + '\n'
                        '    UINT16 available = p_buf->len - 3;\n'
                        '    if (pkt_size > available) pkt_size = (UINT8)available;\n')
        (output / Path(name).name).write_text(text)


if __name__ == '__main__':
    prepare(sys.argv[1], sys.argv[2])
