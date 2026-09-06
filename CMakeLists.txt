cmake_minimum_required(VERSION 3.24)

project(WhisperFlowClone
    VERSION 0.2.0
    DESCRIPTION "Local speech-to-text capture pipeline (open source WhisperFlow clone for Windows)"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Project options
# ---------------------------------------------------------------------------
option(WHISPERFLOW_BUILD_TESTS "Build WhisperFlowClone unit tests" OFF)
option(WHISPERFLOW_USE_VULKAN
       "Optional Vulkan backend. Requires the Vulkan SDK (glslc + SPIRV-Headers). OFF = pure CPU build."
       OFF)
option(WHISPERFLOW_USE_CUDA
       "Optional CUDA backend. Requires the CUDA toolkit. Never required for a working build."
       OFF)
option(WHISPERFLOW_NATIVE
       "Tune ggml kernels for the CPU of the build machine (maps to GGML_NATIVE)"
       ON)

set(WHISPERFLOW_WHISPER_CPP_URL
    "https://github.com/ggml-org/whisper.cpp/archive/refs/tags/v1.9.3.tar.gz"
    CACHE STRING "whisper.cpp source archive, used when third_party/whisper.cpp is absent")
set(WHISPERFLOW_WHISPER_CPP_SHA256
    "1650f884effba487025143bd8facd2f9fb40a83b3737a732803c67a8d659d9c0"
    CACHE STRING "SHA256 of WHISPERFLOW_WHISPER_CPP_URL")

# One predictable output folder for the executable and the test runner.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(_wf_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_wf_cfg} "${CMAKE_BINARY_DIR}/bin")
endforeach()

# ---------------------------------------------------------------------------
# Dependency: whisper.cpp (MIT) and the ggml it bundles (MIT)
#
# Default resolution order:
#   1. third_party/whisper.cpp  - a vendored/offline copy, if you have one
#   2. FetchContent archive     - pinned tag + pinned SHA256, no git needed
#
# All whisper.cpp/ggml knobs are written to the cache *before* the subdirectory
# is added: their option() calls only take effect when the cache entry is absent.
# ---------------------------------------------------------------------------
set(WHISPERFLOW_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(WHISPERFLOW_VENDORED_WHISPER "${WHISPERFLOW_THIRD_PARTY_DIR}/whisper.cpp/CMakeLists.txt")

set(BUILD_SHARED_LIBS      OFF CACHE BOOL "" FORCE)   # static link, single self-contained .exe
set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_IS_DEV   OFF CACHE BOOL "" FORCE)
set(WHISPER_CURL           OFF CACHE BOOL "" FORCE)
set(WHISPER_SDL2           OFF CACHE BOOL "" FORCE)
set(WHISPER_COREML         OFF CACHE BOOL "" FORCE)
set(WHISPER_OPENVINO       OFF CACHE BOOL "" FORCE)

set(GGML_CPU               ON  CACHE BOOL "" FORCE)   # the CPU backend is always built
set(GGML_BLAS              OFF CACHE BOOL "" FORCE)   # no external math library required
set(GGML_METAL             OFF CACHE BOOL "" FORCE)
set(GGML_OPENMP            ON  CACHE BOOL "" FORCE)   # multithreaded CPU inference
set(GGML_NATIVE            ${WHISPERFLOW_NATIVE}      CACHE BOOL "" FORCE)
set(GGML_VULKAN            ${WHISPERFLOW_USE_VULKAN}  CACHE BOOL "" FORCE)
set(GGML_CUDA              ${WHISPERFLOW_USE_CUDA}    CACHE BOOL "" FORCE)

if(EXISTS "${WHISPERFLOW_VENDORED_WHISPER}")
    message(STATUS "whisper.cpp: using the vendored copy in third_party/whisper.cpp")
    add_subdirectory("${WHISPERFLOW_THIRD_PARTY_DIR}/whisper.cpp"
                     "${CMAKE_BINARY_DIR}/whisper.cpp"
                     EXCLUDE_FROM_ALL)
else()
    message(STATUS "whisper.cpp: downloading ${WHISPERFLOW_WHISPER_CPP_URL}")
    include(FetchContent)
    FetchContent_Declare(whisper_cpp
        URL      "${WHISPERFLOW_WHISPER_CPP_URL}"
        URL_HASH "SHA256=${WHISPERFLOW_WHISPER_CPP_SHA256}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(whisper_cpp)
endif()

if(NOT TARGET whisper)
    message(FATAL_ERROR "whisper.cpp did not provide the 'whisper' target")
endif()

# ---------------------------------------------------------------------------
# Strict warning policy, applied to our own code only
# ---------------------------------------------------------------------------
function(whisperflow_apply_strict_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /utf-8
            /external:W0
            /external:anglebrackets
        )
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
        )
    endif()

    # whisper.cpp/ggml headers are C headers consumed from C++20: keep their
    # diagnostics out of our /WX (and -Werror) budget without weakening our code.
    foreach(_wf_dep whisper ggml ggml-base)
        if(TARGET ${_wf_dep})
            get_target_property(_wf_inc ${_wf_dep} INTERFACE_INCLUDE_DIRECTORIES)
            if(_wf_inc)
                target_include_directories(${target} SYSTEM PRIVATE ${_wf_inc})
            endif()
        endif()
    endforeach()
endfunction()

# ---------------------------------------------------------------------------
# Core library: model lookup, configuration, WAV I/O and the whisper.cpp wrapper.
# Shared by the application and the test runner.
# ---------------------------------------------------------------------------
add_library(whisperflow_core STATIC
    src/AppConfig.cpp
    src/Autostart.cpp
    src/HotkeySpec.cpp
    src/ModelLocator.cpp
    src/PhraseHistory.cpp
    src/Settings.cpp
    src/SpeechGate.cpp
    src/TextInjector.cpp
    src/Transcriber.cpp
    src/Vad.cpp
    src/WavFile.cpp
)

target_include_directories(whisperflow_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(whisperflow_core PUBLIC whisper)
if(WIN32)
    target_link_libraries(whisperflow_core PUBLIC advapi32)
endif()
whisperflow_apply_strict_warnings(whisperflow_core)

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------
add_executable(WhisperFlowClone
    src/main.cpp
)

target_link_libraries(WhisperFlowClone PRIVATE whisperflow_core)
whisperflow_apply_strict_warnings(WhisperFlowClone)

if(WIN32)
    # Windows-subsystem executable: the tray build starts without a console window.
    # The WinMain wrapper in main.cpp delegates to main() after UTF-8 argv conversion.
    set_target_properties(WhisperFlowClone PROPERTIES WIN32_EXECUTABLE TRUE)

    # WASAPI microphone capture lives behind this flag so that the rest of the
    # pipeline (model lookup, inference, WAV input) stays buildable elsewhere.
    target_sources(WhisperFlowClone PRIVATE
        src/AudioCapture.cpp
        src/Hotkey.cpp
        src/Overlay.cpp
        src/TrayIcon.cpp
        assets/app.rc
    )
    target_link_libraries(WhisperFlowClone PRIVATE ole32 uuid shell32)
    target_compile_definitions(WhisperFlowClone PRIVATE WHISPERFLOW_HAS_MICROPHONE=1)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(WHISPERFLOW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
