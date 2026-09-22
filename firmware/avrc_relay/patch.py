"""Add the AVRCP target responses omitted by ESP-IDF 5.5.5's public layer."""
import hashlib
from pathlib import Path
import sys

DIGEST = 'c16e573ffea5f52daaaa958fb593d8fbbff49e6fb5bfec9fef44e53b9d5f03dc'


def replace_once(text, marker, replacement, name):
    if text.count(marker) != 1:
        raise RuntimeError(f'Review AVRCP patch marker: {name}')
    return text.replace(marker, replacement)


def prepare(bt_dir, output):
    source = Path(bt_dir) / 'host/bluedroid/btc/profile/std/avrc/btc_avrc.c'
    original = source.read_bytes()
    if hashlib.sha256(original).hexdigest() != DIGEST:
        raise RuntimeError('Review AVRCP relay patch for changed SDK')
    text = original.decode()

    text = replace_once(text,
        'const static uint16_t cs_rn_allowed_evt = \\\n        0x2000;',
        'const static uint16_t cs_rn_allowed_evt = \\\n        0x2026;', 'notification capability')

    marker = '''/*****************************************************************************
**  Externs
******************************************************************************/
'''
    hooks = marker + '''
/* Application-owned media state. ESP-IDF 5.5.5 parses these target commands
 * but otherwise rejects them, so keep this narrow adapter pinned to that SDK. */
__attribute__((weak)) uint16_t dashbridge_avrc_metadata_value(UINT32 attr_id, UINT8 *out,
                                                              UINT16 capacity)
{
    (void)attr_id; (void)out; (void)capacity;
    return 0;
}

__attribute__((weak)) void dashbridge_avrc_play_status(UINT32 *song_len, UINT32 *song_pos,
                                                        UINT8 *play_status)
{
    *song_len = 0; *song_pos = 0; *play_status = AVRC_PLAYSTATE_ERROR;
}
'''
    text = replace_once(text, marker, hooks, 'application hooks')

    marker = '''    case AVRC_PDU_GET_PLAY_STATUS:
    case AVRC_PDU_GET_ELEMENT_ATTR:
    case AVRC_PDU_INFORM_DISPLAY_CHARSET:
'''
    response = '''    case AVRC_PDU_GET_PLAY_STATUS: {
        tAVRC_RESPONSE avrc_rsp;
        memset(&avrc_rsp, 0, sizeof(avrc_rsp));
        avrc_rsp.get_play_status.opcode = opcode_from_pdu(AVRC_PDU_GET_PLAY_STATUS);
        avrc_rsp.get_play_status.pdu = AVRC_PDU_GET_PLAY_STATUS;
        avrc_rsp.get_play_status.status = AVRC_STS_NO_ERROR;
        dashbridge_avrc_play_status(&avrc_rsp.get_play_status.song_len,
                                    &avrc_rsp.get_play_status.song_pos,
                                    &avrc_rsp.get_play_status.play_status);
        send_metamsg_rsp(btc_rc_cb.rc_handle, label, AVRC_CMD_STATUS, &avrc_rsp);
    }
    break;
    case AVRC_PDU_GET_ELEMENT_ATTR: {
        tAVRC_RESPONSE avrc_rsp;
        tAVRC_ATTR_ENTRY entries[AVRC_MAX_ELEM_ATTR_SIZE];
        UINT8 values[AVRC_MAX_ELEM_ATTR_SIZE][129];
        UINT8 count = pavrc_cmd->get_elem_attrs.num_attr;
        if (count > AVRC_MAX_ELEM_ATTR_SIZE) count = AVRC_MAX_ELEM_ATTR_SIZE;
        memset(&avrc_rsp, 0, sizeof(avrc_rsp));
        memset(entries, 0, sizeof(entries));
        if (count == 0) {
            count = 7;
            for (UINT8 i = 0; i < count; ++i) pavrc_cmd->get_elem_attrs.attrs[i] = i + 1;
        }
        for (UINT8 i = 0; i < count; ++i) {
            entries[i].attr_id = pavrc_cmd->get_elem_attrs.attrs[i];
            entries[i].name.charset_id = AVRC_CHARSET_ID_UTF8;
            entries[i].name.p_str = values[i];
            entries[i].name.str_len = dashbridge_avrc_metadata_value(entries[i].attr_id,
                                                                      values[i], sizeof(values[i]));
            if (entries[i].name.str_len > sizeof(values[i])) entries[i].name.str_len = sizeof(values[i]);
        }
        avrc_rsp.get_elem_attrs.opcode = opcode_from_pdu(AVRC_PDU_GET_ELEMENT_ATTR);
        avrc_rsp.get_elem_attrs.pdu = AVRC_PDU_GET_ELEMENT_ATTR;
        avrc_rsp.get_elem_attrs.status = AVRC_STS_NO_ERROR;
        avrc_rsp.get_elem_attrs.num_attr = count;
        avrc_rsp.get_elem_attrs.p_attrs = entries;
        send_metamsg_rsp(btc_rc_cb.rc_handle, label, AVRC_CMD_STATUS, &avrc_rsp);
    }
    break;
    case AVRC_PDU_INFORM_DISPLAY_CHARSET:
'''
    text = replace_once(text, marker, response, 'metadata commands')

    marker = '''    switch (event_id) {
    case ESP_AVRC_RN_VOLUME_CHANGE:
        avrc_rsp.reg_notif.param.volume = param->volume;
        break;
    // todo: implement other event notifications
    default:
'''
    replacement = '''    switch (event_id) {
    case ESP_AVRC_RN_PLAY_STATUS_CHANGE:
        avrc_rsp.reg_notif.param.play_status = param->playback;
        break;
    case ESP_AVRC_RN_TRACK_CHANGE:
        memcpy(avrc_rsp.reg_notif.param.track, param->elm_id, sizeof(param->elm_id));
        break;
    case ESP_AVRC_RN_PLAY_POS_CHANGED:
        avrc_rsp.reg_notif.param.play_pos = param->play_pos;
        break;
    case ESP_AVRC_RN_VOLUME_CHANGE:
        avrc_rsp.reg_notif.param.volume = param->volume;
        break;
    default:
'''
    text = replace_once(text, marker, replacement, 'notification responses')

    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    (output / source.name).write_text(text)


if __name__ == '__main__':
    prepare(sys.argv[1], sys.argv[2])
