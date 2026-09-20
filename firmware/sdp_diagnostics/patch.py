"""Prepare build-local ESP-IDF SDP fixes and diagnostics; never modify the SDK."""
import hashlib
from pathlib import Path
import sys


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise RuntimeError('Review SDP patch: source marker changed')
    return source.replace(old, new)


def prepare(bt_dir, output):
    root = Path(bt_dir) / 'host/bluedroid/stack/sdp'
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    expected = {
        'sdp_server.c': '5e6c87536f3f3d39dca1eaa1c7cfb92da4ed295aacef790c8182c9c29f944e4e',
        'sdp_utils.c': '65b747949a487c65dd1984b035f4e399ef481e7086d6eb81b39a2612cc367cbd',
        'include/sdpint.h': '949e6b31d6f66c0dd093f3140bb7a7d39bb741f058bf846cd7dd14818b9312a4',
    }
    sources = {}
    for name, digest in expected.items():
        data = (root / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise RuntimeError(f'Review SDP patch for changed SDK source: {name}')
        sources[name] = data.decode()

    # Every Bluetooth translation unit must use this same structure layout.
    header = replace_once(sources['include/sdpint.h'],
                          '#define     MAX_ATTR_PER_SEQ        8',
                          '#define     MAX_ATTR_PER_SEQ        16')
    (output / 'sdpint.h').write_text(header)
    utils = sources['sdp_utils.c']
    start = utils.index('UINT8 *sdpu_extract_attr_seq (')
    end = utils.index('\n}\n', start) + 3
    parser = utils[start:end]
    # Check capacity BEFORE writing. The old post-increment check rejected an
    # exactly full list (Tesla MAP asks for 8, Device ID asks for 10 attributes).
    parser = replace_once(parser,
        '    for ( ; p < p_end_list ; ) {',
        '    for ( ; p < p_end_list ; ) {\n'
        '        if (p_seq->num_attr >= MAX_ATTR_PER_SEQ) {\n'
        '            return (NULL);\n'
        '        }')
    parser = replace_once(parser,
        '        if (++p_seq->num_attr >= MAX_ATTR_PER_SEQ) {\n'
        '            return (NULL);\n'
        '        }',
        '        ++p_seq->num_attr;')
    (output / 'sdp_utils.c').write_text(utils[:start] + parser + utils[end:])

    server = sources['sdp_server.c']
    diagnostic = r'''    /* DashBridge: bounded discovery diagnostics, not message data. */
    {
        static const char hex[] = "0123456789abcdef";
        char dump[96 * 2 + 1];
        UINT16 length = p_msg->len < 96 ? p_msg->len : 96;
        for (UINT16 i = 0; i < length; ++i) {
            dump[i * 2] = hex[p_req[i] >> 4];
            dump[i * 2 + 1] = hex[p_req[i] & 15];
        }
        dump[length * 2] = 0;
        SDP_TRACE_WARNING ("DashBridge SDP request cid=0x%x bytes=%u data=%s\n",
                           p_ccb->connection_id, p_msg->len, dump);
    }

'''
    marker = '    /* Start inactivity timer */'
    server = replace_once(server, marker, diagnostic + marker)
    for reason in ['BAD_PDU', 'BAD_UUID_LIST', 'BAD_MAX_RECORDS_LIST', 'BAD_ATTR_LIST']:
        call = f'sdpu_build_n_send_error (p_ccb, trans_num, SDP_INVALID_REQ_SYNTAX, SDP_TEXT_{reason});'
        server = server.replace(call,
            f'SDP_TRACE_WARNING ("DashBridge SDP rejected: {reason}\\n");\n        {call}')
    (output / 'sdp_server.c').write_text(server)


if __name__ == '__main__':
    prepare(sys.argv[1], sys.argv[2])
