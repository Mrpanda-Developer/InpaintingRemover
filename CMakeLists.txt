cmake_minimum_required(VERSION 3.20)
project(subtitle_remover VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(SUBTITLE_REMOVER_BUILD_TESTS "Build unit and integration harnesses" ON)
option(SUBTITLE_REMOVER_WITH_ONNXRUNTIME "Enable ONNX Runtime integration when available" OFF)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale)
find_package(OpenCV 4.5 REQUIRED COMPONENTS core imgproc video)

if(SUBTITLE_REMOVER_WITH_ONNXRUNTIME)
    find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
        PATH_SUFFIXES onnxruntime/core/session include)
    find_library(ONNXRUNTIME_LIBRARY onnxruntime)
    if(NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIBRARY)
        message(FATAL_ERROR "SUBTITLE_REMOVER_WITH_ONNXRUNTIME=ON but ONNX Runtime was not found")
    endif()
endif()

add_library(subtitle_remover_lib
    src/cli.cpp
    src/chunk_reader.cpp
    src/inpainter.cpp
    src/pipeline.cpp
    src/video_reader.cpp
    src/video_writer.cpp
)
target_include_directories(subtitle_remover_lib PUBLIC include ${OpenCV_INCLUDE_DIRS})
target_link_libraries(subtitle_remover_lib PUBLIC PkgConfig::FFMPEG ${OpenCV_LIBS} Threads::Threads)
target_compile_features(subtitle_remover_lib PUBLIC cxx_std_20)
if(SUBTITLE_REMOVER_WITH_ONNXRUNTIME)
    target_include_directories(subtitle_remover_lib PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
    target_link_libraries(subtitle_remover_lib PRIVATE ${ONNXRUNTIME_LIBRARY})
    target_compile_definitions(subtitle_remover_lib PUBLIC SUBTITLE_REMOVER_WITH_ONNXRUNTIME=1)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(subtitle_remover_lib PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_executable(subtitle-remover src/main.cpp)
target_link_libraries(subtitle-remover PRIVATE subtitle_remover_lib)

if(SUBTITLE_REMOVER_BUILD_TESTS)
    enable_testing()
    add_executable(chunk-timeline-tests tests/test_chunk_timeline.cpp)
    target_link_libraries(chunk-timeline-tests PRIVATE subtitle_remover_lib)
    add_test(NAME chunk-timeline-tests COMMAND chunk-timeline-tests)

    add_executable(passthrough-integration tests/integration_harness.cpp)
    target_link_libraries(passthrough-integration PRIVATE subtitle_remover_lib)
endif()