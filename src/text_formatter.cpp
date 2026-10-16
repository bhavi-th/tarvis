#include "text_formatter.h"
#include <string>

std::string format_text_for_terminal(const std::string& input) {
    std::string result = "";
    bool bold = false;
    
    for (size_t i = 0; i < input.length(); ++i) {
        // Handle markdown bold (**text**)
        if (i + 1 < input.length() && input[i] == '*' && input[i+1] == '*') {
            bold = !bold;
            if (bold) {
                result += "\033[1m"; // ANSI code for Bold Start
            } else {
                result += "\033[0m"; // ANSI code for Reset
            }
            i++; // skip second asterisk
            continue;
        }
        
        // Handle literal escape sequence "\n" or actual newline '\n'
        if (i + 1 < input.length() && input[i] == '\\' && input[i+1] == 'n') {
            result += '\n';
            i++;
            continue;
        }

        result += input[i];
    }
    
    // Ensure terminal attributes are reset at the end
    result += "\033[0m";
    return result;
}

std::string clean_text_for_tts(const std::string& input) {
    std::string result = "";
    
    for (size_t i = 0; i < input.length(); ++i) {
        // Skip asterisks
        if (input[i] == '*') {
            continue;
        }
        // Handle literal "\n" or actual newlines -> convert to space for smooth TTS reading
        if (i + 1 < input.length() && input[i] == '\\' && input[i+1] == 'n') {
            result += ' ';
            i++;
            continue;
        }
        if (input[i] == '\n' || input[i] == '\r') {
            result += ' ';
            continue;
        }
        
        result += input[i];
    }

    // Clean up extra double spaces
    std::string final_output = "";
    bool last_was_space = false;
    for (char c : result) {
        if (c == ' ') {
            if (!last_was_space) {
                final_output += c;
                last_was_space = true;
            }
        } else {
            final_output += c;
            last_was_space = false;
        }
    }

    return final_output;
}
