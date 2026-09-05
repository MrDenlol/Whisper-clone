add_executable(WhisperFlowTests
    test_main.cpp
    test_config.cpp
    test_model_locator.cpp
    test_transcriber.cpp
    test_wav_file.cpp
)

target_link_libraries(WhisperFlowTests PRIVATE whisperflow_core)
whisperflow_apply_strict_warnings(WhisperFlowTests)

add_test(NAME WhisperFlowTests COMMAND WhisperFlowTests)
