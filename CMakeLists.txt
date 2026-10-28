cmake_minimum_required(VERSION 3.16)

project(DigitalImageAnalyzer VERSION 0.1.1 LANGUAGES CXX)

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

option(BUILD_TEST "Build unit tests" ON)

# ========= Unit Tests =========
if (BUILD_TEST)
    include(FetchContent)
    add_custom_target(all_tests)
    enable_testing()
    message(STATUS "BUILD_TEST enabled")

    add_library(test_warning_fix INTERFACE)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(test_warning_fix INTERFACE "-Wno-maybe-uninitialized")
    endif ()

    set(GTEST_SOURCE_DIR "${PROJECT_SOURCE_DIR}/third_party/googletest")
    if (EXISTS "${GTEST_SOURCE_DIR}/CMakeLists.txt")
        message(STATUS "[TEST] Use local googletest at third_party/googletest")
        add_subdirectory(${GTEST_SOURCE_DIR} EXCLUDE_FROM_ALL)
    else ()
        message(STATUS "[TEST] Fetch googletest from GitHub")
        FetchContent_Declare(
                googletest
                GIT_REPOSITORY https://github.com/google/googletest.git
                GIT_TAG v1.15.2
                SOURCE_DIR "${GTEST_SOURCE_DIR}"
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif ()

    include(GoogleTest)

    set(DIA_CORE_SOURCES
            core/bitstreamparser.cpp
            core/platformfile.cpp
            core/expgolomb.cpp
            core/h264types.cpp
            core/h265types.cpp
            core/seiparser.cpp
            core/nalanalyzer.cpp
    )

    set(TEST_SOURCES
            test/test_expgolomb.cpp
            test/test_parser.cpp
            test/test_sei.cpp
    )

    foreach (TEST_SOURCE ${TEST_SOURCES})
        get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
        add_executable(${TEST_NAME} ${TEST_SOURCE} ${DIA_CORE_SOURCES})
        target_include_directories(${TEST_NAME} PRIVATE
                ${CMAKE_SOURCE_DIR}
                ${CMAKE_SOURCE_DIR}/core)
        target_compile_definitions(${TEST_NAME} PRIVATE
                TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/test")
        set(BASE_TEST_LIBS
                gmock
                gtest_main
        )
        target_link_libraries(${TEST_NAME}
                PRIVATE
                ${BASE_TEST_LIBS}
                test_warning_fix
        )
        set_target_properties(${TEST_NAME} PROPERTIES FOLDER "tests")

        gtest_discover_tests(${TEST_NAME})
        add_dependencies(all_tests ${TEST_NAME})
    endforeach ()
endif ()
