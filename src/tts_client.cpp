#include "tts_client.h"
#include <iostream>
#include <cstdio>

void speak_text(const std::string& text) {
    if (text.empty()) return;

    // Path to your piper binary and voice model
    std::string piper_path = "piper-tts"; // or absolute path like "/home/bhavith/Projects/tarvis/bin/piper"
    std::string model_path = "/home/bhavith/Projects/tarvis/models/tts/en_US-lessac-medium.onnx";

    // Escape single quotes for shell safety
    std::string safe_text = text;
    size_t pos = 0;
    while ((pos = safe_text.find("'", pos)) != std::string::npos) {
        safe_text.replace(pos, 1, "'\\''");
        pos += 4;
    }

    // Pipe text to Piper -> PipeWire Audio Player (pw-play or aplay)
    std::string command = "echo '" + safe_text + "' | " + piper_path + 
                          " --model " + model_path + 
                          " -f - | pw-play -";

    std::cout << "[Tarvis TTS] Speaking..." << std::endl;
    int ret = std::system(command.c_str());
    if (ret != 0) {
        std::cerr << "[Tarvis Error] TTS synthesis/playback failed!" << std::endl;
    }
}
