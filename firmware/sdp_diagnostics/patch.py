"""Prepare build-local ESP-IDF SDP fixes and diagnostics; never modify the SDK."""
import hashlib
from pathlib import Path
import sys

TRACE_HEADER = r'''/* Discovery packets only: never RFCOMM message or audio payloads. */
static void dashbridge_sdp_trace(const char *direction, UINT16 cid, const BT_HDR *msg)
{
    static const char hex[] = "0123456789abcdef";
    char dump[192 * 2 + 1];
    const UINT8 *data = (const UINT8 *)(msg + 1) + msg->offset;
    UINT16 length = msg->len < 192 ? msg->len : 192;
    for (UINT16 i = 0; i < length; ++i) {
        dump[i * 2] = hex[data[i] >> 4];
        dump[i * 2 + 1] = hex[data[i] & 15];
    }
    dump[length * 2] = 0;
    SDP_TRACE_WARNING("DashBridge SDP %s cid=0x%x bytes=%u captured=%u data=%s\n",
                      direction, cid, msg->len, length, dump);
}
'''


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
    prepare_reconnect(bt_dir, output)
    prepare_connection_trace(bt_dir, output)


def prepare_reconnect(bt_dir, output):
    root = Path(bt_dir) / 'host/bluedroid'
    expected = {
        'stack/sdp/sdp_discovery.c': '979ece05b4cf2295a61cbaca08eb0b59f3131baf8b95729c8ad7a58e61eede44',
        'bta/hf_ag/bta_ag_sdp.c': '2997cc3d26820ad5e76c9ebd21c2315489be5b568c093f47859577c060b2bf02',
        'bta/hf_ag/bta_ag_act.c': '90ad7bf907c469a21b0d2b08fcd6e19978739f96f7209b279d4494838624e1b3',
        'bta/hf_ag/bta_ag_rfc.c': '84a47d218f3e89c948da58fb16b0f2af89c332663e604301d3b1ebb396826e9e',
        'stack/btm/btm_acl.c': '0d8ada1177223c0751cbda9215c90e70865dce4d3d8df3b97dbdb78ae9369f40',
    }
    sources = {}
    for name, digest in expected.items():
        data = (root / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise RuntimeError(f'Review reconnect trace for changed SDK source: {name}')
        sources[Path(name).name] = data.decode()
    (output / 'dashbridge_sdp_trace.h').write_text(TRACE_HEADER)

    client = sources['sdp_discovery.c']
    client = replace_once(client, '#include "sdpint.h"',
                          '#include "sdpint.h"\n#include "dashbridge_sdp_trace.h"')
    client = replace_once(client, '    /* Got a reply!! Check what we got back */',
                          '    dashbridge_sdp_trace("client RX", p_ccb->connection_id, p_msg);\n\n'
                          '    /* Got a reply!! Check what we got back */')
    for buffer, count in [('p_cmd', 1), ('p_msg', 2)]:
        marker = f'L2CA_DataWrite (p_ccb->connection_id, {buffer});'
        if client.count(marker) != count:
            raise RuntimeError('Review SDP client transmit trace locations')
        client = client.replace(marker,
            f'dashbridge_sdp_trace("client TX", p_ccb->connection_id, {buffer});\n    ' + marker)
    sources['sdp_discovery.c'] = client

    server = (output / 'sdp_server.c').read_text()
    server = replace_once(server, '#include "sdpint.h"',
                          '#include "sdpint.h"\n#include "dashbridge_sdp_trace.h"')
    marker = 'L2CA_DataWrite (p_ccb->connection_id, p_buf);'
    if server.count(marker) != 3:
        raise RuntimeError('Review SDP server transmit trace locations')
    server = server.replace(marker,
        'dashbridge_sdp_trace("server TX", p_ccb->connection_id, p_buf);\n    ' + marker)
    (output / 'sdp_server.c').write_text(server)

    ag = sources['bta_ag_sdp.c']
    ag = replace_once(ag, '    APPL_TRACE_DEBUG("bta_ag_sdp_cback status:0x%x", status);',
        '    APPL_TRACE_WARNING("DashBridge HFP discovery complete: status=0x%x slot=%u", status, idx);')
    ag = replace_once(ag, '    /* allocate buffer for sdp database */',
        '    APPL_TRACE_WARNING("DashBridge HFP discovery start: role=%u uuid=0x%04x second_uuid=0x%04x attrs=%u",\n'
        '                       p_scb->role, uuid_list[0].uu.uuid16,\n'
        '                       num_uuid > 1 ? uuid_list[1].uu.uuid16 : 0, num_attr);\n\n'
        '    /* allocate buffer for sdp database */')
    ag = replace_once(ag, '    return result;\n}\n\n/*******************************************************************************\n**\n** Function         bta_ag_do_disc',
        '    APPL_TRACE_WARNING("DashBridge HFP service result: uuid=0x%04x found=%u channel=%u version=0x%x",\n'
        '                       uuid, result, result ? p_scb->peer_scn : 0, p_scb->peer_version);\n'
        '    return result;\n}\n\n/*******************************************************************************\n**\n** Function         bta_ag_do_disc')
    ag = replace_once(ag, '    if(!db_inited) {',
        '    if(!db_inited) {\n'
        '        APPL_TRACE_WARNING("DashBridge HFP discovery could not start");')
    sources['bta_ag_sdp.c'] = ag

    action = sources['bta_ag_act.c']
    action = replace_once(action, '    tBTA_AG_OPEN    open;',
        '    tBTA_AG_OPEN    open;\n'
        '    APPL_TRACE_WARNING("DashBridge HFP open result: status=%u role=%u (ok=%u sdp=%u rfcomm=%u resources=%u)",\n'
        '                       status, p_scb->role, BTA_AG_SUCCESS, BTA_AG_FAIL_SDP,\n'
        '                       BTA_AG_FAIL_RFCOMM, BTA_AG_FAIL_RESOURCES);')
    sources['bta_ag_act.c'] = action

    rfc = sources['bta_ag_rfc.c']
    rfc = replace_once(rfc,
        '    APPL_TRACE_DEBUG("ag_mgmt_cback : code = %d, port_handle = %d, handle = %d",',
        '    APPL_TRACE_WARNING("DashBridge HFP RFCOMM event: code=%d port=%d slot=%d",')
    rfc = replace_once(rfc, '    if (RFCOMM_CreateConnection(bta_ag_uuid[p_scb->conn_service], p_scb->peer_scn,',
        '    APPL_TRACE_WARNING("DashBridge HFP RFCOMM opening: channel=%u security=0x%x",\n'
        '                       p_scb->peer_scn, p_scb->cli_sec_mask);\n'
        '    if (RFCOMM_CreateConnection(bta_ag_uuid[p_scb->conn_service], p_scb->peer_scn,')
    sources['bta_ag_rfc.c'] = rfc
    # A failed feature read for a vanished controller handle is not evidence
    # of changed peer capabilities. Do not feed incomplete pages into the SC
    # downgrade detector, or continue establishment on that dead connection.
    # Successful feature reads and genuine downgrade checks are untouched.
    sources['btm_acl.c'] = replace_once(sources['btm_acl.c'],
        '    /* Process supported features only */',
        '    if (status == HCI_ERR_NO_CONNECTION) {\n'
        '        BTM_TRACE_WARNING("DashBridge: ignoring feature result for closed link; security state unchanged\\n");\n'
        '        return;\n'
        '    }\n\n'
        '    /* Process supported features only */')
    for name, source in sources.items():
        (output / name).write_text(source)


def prepare_connection_trace(bt_dir, output):
    root = Path(bt_dir) / 'host/bluedroid'
    expected = {
        'main/bte_main.c': '17a3420f783c5fe8bf7b1f0598ab2c0c836315eee1a8365fbb7d35a50c1a9d84',
        'stack/btu/btu_hcif.c': 'c71211a2ee18dd019105f418ca0f8d7b06856b50d05a41875d0815cbb85b116f',
        'stack/l2cap/l2c_main.c': '4b9f998bb32f35f1a8e3aa59cf535df7006863e0ca3172165afe9a3797649e8a',
        'stack/l2cap/l2c_utils.c': '7eba40f7a32af8ec861558109c3bb7931973e72ea9b85f01387ce0c43db9a56d',
        'stack/l2cap/l2c_csm.c': 'a2ddc4d4d0fd366b3881311132b677972f187ef9b7bddb846e7597da3091ba8b',
        'stack/sdp/sdp_main.c': '35145fda2f3a242ceb82582549a5c1e009e15dcdd6c69f76ff6a163fef5a7e10',
    }
    sources = {}
    for name, digest in expected.items():
        data = (root / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise RuntimeError(f'Review connection trace for changed SDK source: {name}')
        sources[Path(name).name] = data.decode()
    (output / 'connection_trace.h').write_bytes(Path(__file__).with_name('connection_trace.h').read_bytes())
    for name in sources:
        sources[name] = '#include "connection_trace.h"\n' + sources[name]

    sources['bte_main.c'] = replace_once(sources['bte_main.c'],
        '        hci->transmit_downward(event, p_msg);',
        '        if ((event & BT_EVT_MASK) == BT_EVT_TO_LM_HCI_ACL)\n'
        '            db_l2cap_signal("TX-controller", p_msg->data + p_msg->offset, p_msg->len);\n'
        '        hci->transmit_downward(event, p_msg);')
    hci = sources['btu_hcif.c']
    hci = replace_once(hci,
        'void btu_hcif_process_event (UNUSED_ATTR UINT8 controller_id, BT_HDR *p_msg)\n{',
        'void btu_hcif_process_event (UNUSED_ATTR UINT8 controller_id, BT_HDR *p_msg)\n{\n'
        '    db_hci_event(p_msg->data + p_msg->offset, p_msg->len);')
    marker = '    assert (p_buf->layer_specific == HCI_CMD_BUF_TYPE_METADATA);'
    start = hci.index('void btu_hcif_send_cmd (')
    end = hci.index('\n}\n', start) + 3
    body = replace_once(hci[start:end], marker,
        '    db_hci_command(p_buf->data + p_buf->offset, p_buf->len, -1);\n' + marker)
    hci = hci[:start] + body + hci[end:]
    for kind, trace in [
        ('complete', 'db_hci_event(hack->response->data + hack->response->offset, hack->response->len);'),
        ('status', 'db_hci_command(hack->command->data + hack->command->offset, hack->command->len, hack->status);'),
    ]:
        marker = (f'static void btu_hcif_command_{kind}_evt_on_task(BT_HDR *event)\n{{\n'
                  f'    command_{kind}_hack_t *hack = (command_{kind}_hack_t *)&event->data[0];')
        hci = replace_once(hci, marker, marker + '\n    ' + trace)
    sources['btu_hcif.c'] = hci
    sources['l2c_main.c'] = replace_once(sources['l2c_main.c'],
        'void l2c_rcv_acl_data (BT_HDR *p_msg)\n{',
        'void l2c_rcv_acl_data (BT_HDR *p_msg)\n{\n'
        '    db_l2cap_signal("RX", p_msg->data + p_msg->offset, p_msg->len);')
    utils = sources['l2c_utils.c']
    import re
    pattern = r'(?m)^(\s*)l2c_link_check_send_pkts \(([^\n]*), (p_buf2?)\);'
    def trace_send(m):
        return (m[1] + f'db_l2cap_signal("TX-queued", {m[3]}->data + {m[3]}->offset, {m[3]}->len);\n'
                + m[0])
    utils, count = re.subn(pattern, trace_send, utils)
    if count != 21:
        raise RuntimeError(f'Review L2CAP send trace coverage: {count}')
    sources['l2c_utils.c'] = utils
    sources['l2c_csm.c'] = replace_once(sources['l2c_csm.c'],
        'void l2c_csm_execute (tL2C_CCB *p_ccb, UINT16 event, void *p_data)\n{',
        'void l2c_csm_execute (tL2C_CCB *p_ccb, UINT16 event, void *p_data)\n{\n'
        '    /* Open-channel data/audio traffic is deliberately not traced. */\n'
        '    if (p_ccb->chnl_state != CST_OPEN)\n'
        '        DB_TRACE("L2CAP state=%u event=%u cid=0x%x remote=0x%x handle=0x%x",\n'
        '                 p_ccb->chnl_state, event, p_ccb->local_cid, p_ccb->remote_cid,\n'
        '                 p_ccb->p_lcb ? p_ccb->p_lcb->handle : 0xffff);')
    sdp = sources['sdp_main.c']
    for signature, line in [
        ('static void sdp_connect_cfm (UINT16 l2cap_cid, UINT16 result)',
         'DB_TRACE("SDP connect-confirm cid=0x%x result=0x%x", l2cap_cid, result);'),
        ('static void sdp_config_cfm (UINT16 l2cap_cid, tL2CAP_CFG_INFO *p_cfg)',
         'DB_TRACE("SDP config-confirm cid=0x%x result=0x%x", l2cap_cid, p_cfg->result);'),
        ('static void sdp_disconnect_ind (UINT16 l2cap_cid, BOOLEAN ack_needed)',
         'DB_TRACE("SDP remote-disconnect cid=0x%x ack=%u", l2cap_cid, ack_needed);'),
        ('void sdp_disconnect (tCONN_CB *p_ccb, UINT16 reason)',
         'DB_TRACE("SDP local-disconnect cid=0x%x state=%u reason=0x%x", p_ccb->connection_id, p_ccb->con_state, reason);'),
        ('void sdp_conn_timeout (tCONN_CB *p_ccb)',
         'DB_TRACE("SDP timeout cid=0x%x state=%u", p_ccb->connection_id, p_ccb->con_state);'),
    ]:
        sdp = replace_once(sdp, signature + '\n{', signature + '\n{\n    ' + line)
    sources['sdp_main.c'] = sdp
    for name, source in sources.items():
        (output / name).write_text(source)


if __name__ == '__main__':
    prepare(sys.argv[1], sys.argv[2])
