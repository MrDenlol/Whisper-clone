cmake_minimum_required(VERSION 3.24)

project(WhisperFlowClone
    VERSION 0.1.0
    DESCRIPTION "Local speech-to-text capture pipeline"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(WhisperFlowClone
    src/main.cpp
    src/AudioCapture.cpp
)

target_include_directories(WhisperFlowClone
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

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

# Third-party libraries (whisper.cpp, etc.):
#   1. Clone or FetchContent into third_party/<name>
#   2. add_subdirectory(third_party/<name>)
#   3. target_link_libraries(WhisperFlowClone PRIVATE <imported-or-alias-target>)
set(WHISPERFLOW_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# Example (uncomment when whisper.cpp is vendored):
# add_subdirectory("${WHISPERFLOW_THIRD_PARTY_DIR}/whisper.cpp" EXCLUDE_FROM_ALL)
# target_link_libraries(WhisperFlowClone PRIVATE whisper)
