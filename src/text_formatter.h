#pragma once
#include <string>

// Formats markdown (bold, newlines, etc.) into ANSI escape codes for terminal display
std::string format_text_for_terminal(const std::string& input);

// Cleans markdown and ANSI codes completely for clean text-to-speech audio
std::string clean_text_for_tts(const std::string& input);
