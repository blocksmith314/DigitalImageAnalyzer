# 测试流生成器（依赖 FFmpeg 编码器）
add_executable(gen gen.cpp)

target_link_libraries(gen PRIVATE
    PkgConfig::AVFORMAT
    PkgConfig::AVCODEC
    PkgConfig::AVUTIL
    m
)

# 解析核心源码（test_map / test_poc 不需要 FFmpeg，只复用 core 的解析代码）
set(TEST_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/core/bitstreamparser.cpp
    ${CMAKE_SOURCE_DIR}/core/expgolomb.cpp
    ${CMAKE_SOURCE_DIR}/core/h264types.cpp
    ${CMAKE_SOURCE_DIR}/core/h265types.cpp
)

# 打印 H.264 slice header 解析结果（frame_num / POC）
add_executable(test_poc test_poc.cpp ${TEST_CORE_SOURCES})
target_include_directories(test_poc PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core
)

# 校验 NALAnalyzer 的帧分组与 PTS 排序结果
add_executable(test_map test_map.cpp
    ${TEST_CORE_SOURCES}
    ${CMAKE_SOURCE_DIR}/core/nalanalyzer.cpp
)
target_include_directories(test_map PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core
)

# 不设置 RUNTIME_OUTPUT_DIRECTORY，三个目标的产物默认输出到构建目录
# （即 build/test/），源码目录 test/ 只保留源码和已提交的测试码流。
