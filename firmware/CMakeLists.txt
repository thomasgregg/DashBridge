cmake_minimum_required(VERSION 3.16)
if(NOT DASHBRIDGE_ROLE MATCHES "^(phone|car|single)$")
    message(FATAL_ERROR "Build with tools/build.sh, or specify -DDASHBRIDGE_ROLE=phone|car|single")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../tools/build_scope.py"
    "${DASHBRIDGE_ROLE}" --version
    OUTPUT_VARIABLE PROJECT_VER OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE version_result)
if(NOT version_result EQUAL 0)
    message(FATAL_ERROR "Failed to derive firmware version")
endif()
# Source edits must regenerate PROJECT_VER even for incremental IDF builds.
file(GLOB_RECURSE version_inputs CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${version_inputs}
    "${CMAKE_CURRENT_LIST_DIR}/../tools/build_scope.py"
    "${CMAKE_CURRENT_LIST_DIR}/../tools/build.sh"
    "${CMAKE_CURRENT_LIST_DIR}/../tools/package.py")
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
