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
if(CONFIG_BRIDGE_CALL_RELAY)
    include("${CMAKE_CURRENT_LIST_DIR}/audio_codec/CMakeLists.txt")
    include("${CMAKE_CURRENT_LIST_DIR}/conference/CMakeLists.txt")
endif()
if(CONFIG_BRIDGE_MUSIC_RELAY)
    include("${CMAKE_CURRENT_LIST_DIR}/avrc_relay/CMakeLists.txt")
endif()
