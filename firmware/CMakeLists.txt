cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/core/dashbridge_domain_core"
    "${CMAKE_CURRENT_LIST_DIR}/core/runtime_ports"
    "${CMAKE_CURRENT_LIST_DIR}/protocols/obex"
    "${CMAKE_CURRENT_LIST_DIR}/protocols/calls_v3"
    "${CMAKE_CURRENT_LIST_DIR}/protocols/contacts_v1"
    "${CMAKE_CURRENT_LIST_DIR}/protocols/dashlink_v2"
    "${CMAKE_CURRENT_LIST_DIR}/protocols/music_v1"
    "${CMAKE_CURRENT_LIST_DIR}/protocols/setup_gatt_v1"
    "${CMAKE_CURRENT_LIST_DIR}/adapters/car/map_adapter"
    "${CMAKE_CURRENT_LIST_DIR}/adapters/car/pbap_adapter"
    "${CMAKE_CURRENT_LIST_DIR}/adapters/car/esp_tesla_adapter"
    "${CMAKE_CURRENT_LIST_DIR}/adapters/dashbridge_adapter_api"
    "${CMAKE_CURRENT_LIST_DIR}/adapters/calls/esp_hfp_adapter"
    "${CMAKE_CURRENT_LIST_DIR}/adapters/contacts/esp_contacts_adapter"
    "${CMAKE_CURRENT_LIST_DIR}/adapters/music/esp_a2dp_avrcp_adapter"
    "${CMAKE_CURRENT_LIST_DIR}/adapters/phone/setup_gatt_v1_adapter"
    "${CMAKE_CURRENT_LIST_DIR}/adapters/phone/esp_ancs_adapter"
    "${CMAKE_CURRENT_LIST_DIR}/transport/dashlink_transport"
    "${CMAKE_CURRENT_LIST_DIR}/platform/esp_runtime"
    "${CMAKE_CURRENT_LIST_DIR}/apps/dashbridge_app"
)
if(NOT DASHBRIDGE_ROLE MATCHES "^(phone|car)$")
    message(FATAL_ERROR "Build with tools/build.sh, or specify -DDASHBRIDGE_ROLE=phone|car")
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
    "${CMAKE_CURRENT_LIST_DIR}/../version.txt"
    "${CMAKE_CURRENT_LIST_DIR}/../tools/build_scope.py"
    "${CMAKE_CURRENT_LIST_DIR}/../tools/build.sh"
    "${CMAKE_CURRENT_LIST_DIR}/../tools/package.py")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(dashbridge)
if(CONFIG_BRIDGE_CALL_RELAY)
    include("${CMAKE_CURRENT_LIST_DIR}/audio_codec/CMakeLists.txt")
    include("${CMAKE_CURRENT_LIST_DIR}/conference/CMakeLists.txt")
endif()
if(CONFIG_BRIDGE_MUSIC_RELAY)
    include("${CMAKE_CURRENT_LIST_DIR}/avrc_relay/CMakeLists.txt")
endif()
