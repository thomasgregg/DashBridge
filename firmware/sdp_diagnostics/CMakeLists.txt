idf_component_get_property(bt_dir bt COMPONENT_DIR)
idf_component_get_property(bt_lib bt COMPONENT_LIB)
set(patch_script "${CMAKE_CURRENT_LIST_DIR}/patch.py")
set(patched_dir "${CMAKE_BINARY_DIR}/dashbridge_sdp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${patch_script}" "${CMAKE_CURRENT_LIST_DIR}/connection_trace.h")
execute_process(COMMAND "${PYTHON}" "${patch_script}" "${bt_dir}" "${patched_dir}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Failed to prepare the pinned SDP compatibility patch")
endif()
# All Bluetooth source files must use the same SDP structure layout.
target_include_directories(${bt_lib} BEFORE PRIVATE "${patched_dir}")
get_target_property(bt_sources ${bt_lib} SOURCES)
set(replaced 0)
set(updated_sources "")
foreach(item IN LISTS bt_sources)
    if(item MATCHES "(^|/)dm/bta_dm_act\\.c$" OR
       item MATCHES "(^|/)sdp/(sdp_server|sdp_utils|sdp_discovery|sdp_main)\\.c$" OR
       item MATCHES "(^|/)hf_ag/(bta_ag_sdp|bta_ag_act|bta_ag_rfc)\\.c$" OR
       item MATCHES "(^|/)main/bte_main\\.c$" OR
       item MATCHES "(^|/)btu/btu_hcif\\.c$" OR
       item MATCHES "(^|/)l2cap/(l2c_main|l2c_utils|l2c_csm)\\.c$" OR
       item MATCHES "(^|/)btm/btm_acl\\.c$")
        get_filename_component(filename "${item}" NAME)
        list(APPEND updated_sources "${patched_dir}/${filename}")
        math(EXPR replaced "${replaced} + 1")
    else()
        list(APPEND updated_sources "${item}")
    endif()
endforeach()
if(NOT replaced EQUAL 14)
    message(FATAL_ERROR "Could not locate all SDP/HFP/ACL patch sources in Bluetooth target")
endif()
set_property(TARGET ${bt_lib} PROPERTY SOURCES "${updated_sources}")
