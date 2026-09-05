cmake_minimum_required(VERSION 3.24)

project(WhisperFlowClone
    VERSION 0.1.0
    DESCRIPTION "Local speech-to-text capture pipeline"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Project options
option(WHISPERFLOW_BUILD_TESTS "Build WhisperFlowClone unit tests" OFF)

# Main executable
add_executable(WhisperFlowClone
    src/main.cpp
    src/AudioCapture.cpp
)

target_include_directories(WhisperFlowClone
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Compiler warnings and strict compliance settings
if(MSVC)
    target_compile_options(WhisperFlowClone PRIVATE
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
        /utf-8
    )
else()
    target_compile_options(WhisperFlowClone PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()

# Third-party dependencies directory setup
set(WHISPERFLOW_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# Safely include third-party dependencies if they are checked out / vendored
if(EXISTS "${WHISPERFLOW_THIRD_PARTY_DIR}/whisper.cpp/CMakeLists.txt")
    message(STATUS "Found whisper.cpp in third_party/whisper.cpp - enabling build integration")
    add_subdirectory("${WHISPERFLOW_THIRD_PARTY_DIR}/whisper.cpp" EXCLUDE_FROM_ALL)
    target_link_libraries(WhisperFlowClone PRIVATE whisper)
endif()

# Testing infrastructure placeholder
if(WHISPERFLOW_BUILD_TESTS)
    enable_testing()
    message(STATUS "WhisperFlowClone tests enabled")
endif()
