add_executable(WhisperFlowTests
    test_main.cpp
    test_autostart.cpp
    test_config.cpp
    test_hotkey_spec.cpp
    test_model_locator.cpp
    test_phrase_history.cpp
    test_session_guard.cpp
    test_settings.cpp
    test_speech_gate.cpp
    test_text_normalizer.cpp
    test_transcriber.cpp
    test_vad.cpp
    test_wav_file.cpp
)

target_link_libraries(WhisperFlowTests PRIVATE whisperflow_core)
whisperflow_apply_strict_warnings(WhisperFlowTests)

add_test(NAME WhisperFlowTests COMMAND WhisperFlowTests)
