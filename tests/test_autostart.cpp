#include "Autostart.h"
#include "test_framework.h"

#include <filesystem>

WF_TEST(AutostartCommandQuotesAndAddsTrayFlag) {
    const std::filesystem::path exe("C:\\Program Files\\Whisper Flow Clone\\WhisperFlowClone.exe");
    const std::string command = whisperflow::autostartCommand(exe);
    WF_CHECK_EQ(command, std::string("\"C:\\Program Files\\Whisper Flow Clone\\WhisperFlowClone.exe\" --tray"));
}

WF_TEST(AutostartCommandHandlesPlainPaths) {
    const std::filesystem::path exe("/opt/whisper-clone/WhisperFlowClone");
    WF_CHECK_EQ(whisperflow::autostartCommand(exe), std::string("\"/opt/whisper-clone/WhisperFlowClone\" --tray"));
}
