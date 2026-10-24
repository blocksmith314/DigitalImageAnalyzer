# Test stream generator (requires FFmpeg encoders)
add_executable(gen gen.cpp)

target_link_libraries(gen PRIVATE
    PkgConfig::AVFORMAT
    PkgConfig::AVCODEC
    PkgConfig::AVUTIL
    m
)

# Parser core sources shared by test_map / test_poc (no FFmpeg needed)
set(TEST_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/core/bitstreamparser.cpp
    ${CMAKE_SOURCE_DIR}/core/expgolomb.cpp
    ${CMAKE_SOURCE_DIR}/core/h264types.cpp
    ${CMAKE_SOURCE_DIR}/core/h265types.cpp
)

# Prints parsed H.264 slice headers (frame_num / POC)
add_executable(test_poc test_poc.cpp ${TEST_CORE_SOURCES})
target_include_directories(test_poc PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core
)

# Verifies NALAnalyzer frame grouping and PTS ordering
add_executable(test_map test_map.cpp
    ${TEST_CORE_SOURCES}
    ${CMAKE_SOURCE_DIR}/core/nalanalyzer.cpp
)
target_include_directories(test_map PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core
)

# No RUNTIME_OUTPUT_DIRECTORY: binaries go to the build tree (build/test/),
# while test/ keeps sources and the committed test streams only.
