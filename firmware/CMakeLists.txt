cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "0.3.0-call-alpha")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(dashbridge)
if(CONFIG_BRIDGE_CAR OR CONFIG_BRIDGE_SINGLE)
    include(sdp_diagnostics/CMakeLists.txt)
endif()

if(CONFIG_BRIDGE_CALL_RELAY AND CONFIG_BRIDGE_CAR)
    idf_component_get_property(call_bt_lib bt COMPONENT_LIB)
    # Advertise only implemented gateway features: no Siri, in-band ringtone,
    # multiparty support or gateway-side echo cancellation.
    target_compile_definitions(${call_bt_lib} PRIVATE
        "BTC_HF_FEATURES=(BTA_AG_FEAT_REJECT|BTA_AG_FEAT_EXTERR|BTA_AG_FEAT_ESCO_S4|BTA_AG_FEAT_UNAT)")
endif()
