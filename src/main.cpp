#include "whisper.h"
#include <SDL2/SDL.h>
#include <iostream>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <csignal>
#include <cmath>

// Global audio buffer and thread synchronization primitives
std::vector<float> audio_buffer;
std::mutex audio_mutex;
std::atomic<bool> running(true);
std::atomic<long long> total_samples_captured(0);

// Signal handler for Ctrl + C (SIGINT)
void signal_handler(int signal) {
    if (signal == SIGINT) {
        std::cout << "\n[Tarvis] Interrupt received. Shutting down gracefully..." << std::endl;
        running = false;
    }
}

void audio_callback(void *userdata, Uint8 *stream, int len) {
    int n_samples = len / sizeof(int16_t); // 16-bit PCM samples
    int16_t *samples = (int16_t *)stream;

    std::lock_guard<std::mutex> lock(audio_mutex);
    for (int i = 0; i < n_samples; ++i) {
        float sample = (float)samples[i] / 32768.0f;
        audio_buffer.push_back(sample);
        total_samples_captured++;
    }
}

int main() {
    // Register the Ctrl + C signal handler
    std::signal(SIGINT, signal_handler);

    std::cout << "[Tarvis] Initializing local AI voice engine..." << std::endl;

    // 1. Initialize Whisper Model
    struct whisper_context_params cparams = whisper_context_default_params();
    struct whisper_context * ctx = whisper_init_from_file_with_params("/home/bhavith/whisper.cpp/ggml-base.en.bin", cparams);
    if (!ctx) {
        std::cerr << "[Tarvis Error] Failed to load Whisper model!" << std::endl;
        return 1;
    }
    std::cout << "[Tarvis Success] Whisper model loaded into memory!" << std::endl;

    // 2. Initialize SDL2 Audio Subsystem
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        std::cerr << "[Tarvis Error] SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
        whisper_free(ctx);
        return 1;
    }

    // Configure desired audio spec: 16kHz, 16-bit mono PCM
    SDL_AudioSpec desired_spec;
    SDL_zero(desired_spec);
    desired_spec.freq = 16000;
    desired_spec.format = AUDIO_S16SYS;
    desired_spec.channels = 1;
    desired_spec.samples = 1024;
    desired_spec.callback = audio_callback;

    SDL_AudioSpec obtained_spec;
    SDL_AudioDeviceID device_id = SDL_OpenAudioDevice(nullptr, 1, &desired_spec, &obtained_spec, 0);
    if (device_id == 0) {
        std::cerr << "[Tarvis Error] Failed to open audio device: " << SDL_GetError() << std::endl;
        SDL_Quit();
        whisper_free(ctx);
        return 1;
    }

    struct whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_progress = false;
    wparams.language = "en";
    wparams.n_threads = 4;

    std::cout << "\n[Tarvis] Entering Active VAD Listening Mode. (Speak your paragraph, pause to convert)" << std::endl;

    // Start continuous audio recording
    SDL_PauseAudioDevice(device_id, 0);

    // VAD Configuration Parameters
    const float VOICE_THRESHOLD = 0.015f;    // Adjust if your environment is louder/quieter
    const int SILENCE_DURATION_MS = 800;     // Pause duration (ms) required to finalize and process text
    const int CHECK_INTERVAL_MS = 100;       // Buffer check interval

    std::vector<float> speech_buffer;
    bool is_speaking = false;
    int silence_counter = 0;

    // Main continuous listening loop with VAD control
    while (running) {
        SDL_Delay(CHECK_INTERVAL_MS);

        if (!running) {
            break;
        }

        // Safely extract the current audio chunk from the global buffer
        std::vector<float> chunk;
        {
            std::lock_guard<std::mutex> lock(audio_mutex);
            if (audio_buffer.empty()) {
                if (is_speaking) {
                    silence_counter += CHECK_INTERVAL_MS;
                }
                continue;
            }
            chunk = audio_buffer;
            audio_buffer.clear();
        }

        // Calculate Root Mean Square (RMS) energy to check for voice activity
        float sum_squares = 0.0f;
        for (float sample : chunk) {
            sum_squares += sample * sample;
        }
        float rms = std::sqrt(sum_squares / chunk.size());

        // Append chunk to ongoing speech accumulation buffer
        speech_buffer.insert(speech_buffer.end(), chunk.begin(), chunk.end());

        if (rms > VOICE_THRESHOLD) {
            if (!is_speaking) {
                std::cout << "[Tarvis] Speech detected... listening..." << std::endl;
                is_speaking = true;
            }
            silence_counter = 0; // Reset silence counter while user is talking
        } else if (is_speaking) {
            silence_counter += CHECK_INTERVAL_MS;

            // If silence threshold is reached, process the whole spoken phrase/paragraph
            if (silence_counter >= SILENCE_DURATION_MS) {
                std::cout << "[Tarvis] Pause detected. Processing full paragraph..." << std::endl;

                // Ensure there's enough content to warrant inference (at least ~0.5 seconds of data)
                if (speech_buffer.size() > 8000) {
                    if (whisper_full(ctx, wparams, speech_buffer.data(), speech_buffer.size()) == 0) {
                        std::string transcript = "";
                        const int n_segments = whisper_full_n_segments(ctx);
                        for (int i = 0; i < n_segments; ++i) {
                            transcript += whisper_full_get_segment_text(ctx, i);
                        }

                        // Trim leading spaces
                        while (!transcript.empty() && (transcript.front() == ' ' || transcript.front() == '\t')) {
                            transcript.erase(transcript.begin());
                        }

                        if (!transcript.empty() && transcript.find("[BLANK_AUDIO]") == std::string::npos) {
                            std::cout << "\n[Full Transcript]: " << transcript << "\n\n";
                        }
                    } else {
                        std::cerr << "[Tarvis Error] Failed to process audio buffer." << std::endl;
                    }
                }

                // Reset state for the next paragraph
                speech_buffer.clear();
                is_speaking = false;
                silence_counter = 0;
                std::cout << "[Tarvis] Resuming listening..." << std::endl;
            }
        } else {
            // Drop trailing background cache so it doesn't grow infinitely when idle
            if (speech_buffer.size() > 16000 * 2) {
                speech_buffer.erase(speech_buffer.begin(), speech_buffer.end() - 8000);
            }
        }
    }

    // Graceful Cleanup
    std::cout << "[Tarvis] Releasing hardware and memory resources..." << std::endl;
    SDL_PauseAudioDevice(device_id, 1);
    SDL_CloseAudioDevice(device_id);
    SDL_Quit();
    whisper_free(ctx);
    std::cout << "[Tarvis] Exited successfully." << std::endl;

    return 0;
}
