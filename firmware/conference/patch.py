"""Expose parsed Tesla AT+CHLD commands through the pinned public AG callback."""
import hashlib
from pathlib import Path
import sys

DIGEST = '7bf552ec07f280ad3a1a924f392b59d86304aac56dc0ce16bfbb28f9db33c3e3'


def prepare(bt_dir, output):
    source = Path(bt_dir) / 'host/bluedroid/btc/profile/std/hf_ag/btc_hf_ag.c'
    original = source.read_bytes()
    if hashlib.sha256(original).hexdigest() != DIGEST:
        raise RuntimeError('Review conference callback patch for changed SDK')
    text = original.decode()
    marker = '''        case BTA_AG_AT_UNAT_EVT:
        {
'''
    replacement = '''        case BTA_AG_AT_CHLD_EVT:
        {
            idx = p_data->hdr.handle - 1;
            CHECK_HF_IDX(idx);
            char command[16];
            snprintf(command, sizeof(command), "+CHLD=%s", p_data->val.str);
            memcpy(param.unat_rep.remote_addr, &hf_local_param.btc_hf_cb[idx].connected_bda,
                   sizeof(esp_bd_addr_t));
            param.unat_rep.unat = command;
            /* ESP-IDF 5.5 parses CHLD but exposes no public event for it. Reuse
             * the synchronous unknown-AT callback with an unambiguous prefix. */
            btc_hf_cb_to_app(ESP_HF_UNAT_RESPONSE_EVT, &param);
            break;
        }

''' + marker
    if text.count(marker) != 1:
        raise RuntimeError('Review conference callback patch marker')
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    (output / source.name).write_text(text.replace(marker, replacement))


if __name__ == '__main__':
    prepare(sys.argv[1], sys.argv[2])
