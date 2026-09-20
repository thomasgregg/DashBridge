# Instrument a build-local copy, leaving the installed SDK untouched.
# Only SDP discovery metadata is logged; message traffic is not handled here.
idf_component_get_property(bt_dir bt COMPONENT_DIR)
idf_component_get_property(bt_lib bt COMPONENT_LIB)
set(original "${bt_dir}/host/bluedroid/stack/sdp/sdp_server.c")
file(SHA256 "${original}" source_hash)
if(NOT source_hash STREQUAL "5e6c87536f3f3d39dca1eaa1c7cfb92da4ed295aacef790c8182c9c29f944e4e")
    message(FATAL_ERROR "Review SDP diagnostics for this ESP-IDF source version")
endif()
file(READ "${original}" source)
set(diagnostics [=[
    /* DashBridge: bounded service-discovery diagnostics, not message data. */
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

]=])
string(REPLACE "    /* Start inactivity timer */"
    "${diagnostics}    /* Start inactivity timer */" source "${source}")
foreach(reason BAD_PDU BAD_UUID_LIST BAD_MAX_RECORDS_LIST BAD_ATTR_LIST)
    set(call "sdpu_build_n_send_error (p_ccb, trans_num, SDP_INVALID_REQ_SYNTAX, SDP_TEXT_${reason});")
    string(REPLACE "${call}"
        "SDP_TRACE_WARNING (\"DashBridge SDP rejected: ${reason}\\n\");\n        ${call}"
        source "${source}")
endforeach()
set(patched "${CMAKE_BINARY_DIR}/dashbridge_sdp_server.c")
file(WRITE "${patched}" "${source}")
get_target_property(bt_sources ${bt_lib} SOURCES)
set(found FALSE)
set(updated_sources "")
foreach(item IN LISTS bt_sources)
    if(item MATCHES "(^|/)sdp/sdp_server\\.c$")
        list(APPEND updated_sources "${patched}")
        set(found TRUE)
    else()
        list(APPEND updated_sources "${item}")
    endif()
endforeach()
if(NOT found)
    message(FATAL_ERROR "Could not locate SDP source in the Bluetooth target")
endif()
set_property(TARGET ${bt_lib} PROPERTY SOURCES "${updated_sources}")
