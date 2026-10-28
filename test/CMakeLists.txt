# Developer tools (not part of the automated test suite).
# Sources live in test/tools; binaries go to build/test/tools.

add_executable(gen tools/gen.cpp)
set_target_properties(gen PROPERTIES
    FOLDER "tools"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test/tools)
target_link_libraries(gen PRIVATE
    PkgConfig::AVFORMAT
    PkgConfig::AVCODEC
    PkgConfig::AVUTIL
    m
)

set(TOOL_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/core/bitstreamparser.cpp
    ${CMAKE_SOURCE_DIR}/core/platformfile.cpp
    ${CMAKE_SOURCE_DIR}/core/expgolomb.cpp
    ${CMAKE_SOURCE_DIR}/core/h264types.cpp
    ${CMAKE_SOURCE_DIR}/core/h265types.cpp
    ${CMAKE_SOURCE_DIR}/core/seiparser.cpp
)

add_executable(slice_header_dump tools/slice_header_dump.cpp ${TOOL_CORE_SOURCES})
target_include_directories(slice_header_dump PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core)
set_target_properties(slice_header_dump PROPERTIES
    FOLDER "tools"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test/tools)

add_executable(frame_map tools/frame_map.cpp
    ${TOOL_CORE_SOURCES}
    ${CMAKE_SOURCE_DIR}/core/nalanalyzer.cpp)
target_include_directories(frame_map PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core)
set_target_properties(frame_map PROPERTIES
    FOLDER "tools"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test/tools)
