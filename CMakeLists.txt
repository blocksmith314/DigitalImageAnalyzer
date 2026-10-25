cmake_minimum_required(VERSION 3.16)

project(DigitalImageAnalyzer VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia)

qt_standard_project_setup()

find_package(PkgConfig REQUIRED)
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWSCALE REQUIRED IMPORTED_TARGET libswscale)
pkg_check_modules(SWRESAMPLE REQUIRED IMPORTED_TARGET libswresample)

qt_add_executable(DigitalImageAnalyzer
    main.cpp
    mainwindow.h
    mainwindow.cpp
    nallistmodel.h
    nallistmodel.cpp

    core/nalunit.h
    core/platformfile.h
    core/platformfile.cpp
    core/expgolomb.h
    core/expgolomb.cpp
    core/bitstreamparser.h
    core/bitstreamparser.cpp
    core/h264types.h
    core/h264types.cpp
    core/h265types.h
    core/h265types.cpp
    core/nalanalyzer.h
    core/nalanalyzer.cpp
    core/seiparser.h
    core/seiparser.cpp

    ffmpeg/decoder.h
    ffmpeg/decoder.cpp
    ffmpeg/annexb.h
    ffmpeg/annexb.cpp
    ffmpeg/avccparser.cpp
    ffmpeg/audio.h
    ffmpeg/audio.cpp
    ffmpeg/mp4index.cpp
)

target_include_directories(DigitalImageAnalyzer PRIVATE
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/ffmpeg
)

target_link_libraries(DigitalImageAnalyzer PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    PkgConfig::AVFORMAT
    PkgConfig::AVCODEC
    PkgConfig::AVUTIL
    PkgConfig::SWSCALE
    PkgConfig::SWRESAMPLE
    Qt6::Multimedia
)

# The analyzer walks millions of NAL units; unoptimized Debug code is 10-20x
# slower, so keep -O2 even in Debug builds (still with debug info).
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(DigitalImageAnalyzer PRIVATE $<$<CONFIG:Debug>:-O2>)
endif()

set_target_properties(DigitalImageAnalyzer PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

option(BUILD_TEST_GEN "Build H.264/H.265 test stream generator" ON)
if(BUILD_TEST_GEN)
    add_subdirectory(test)
endif()