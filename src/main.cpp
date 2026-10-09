#include "whisper.h"
#include <SDL2/SDL.h>
#include <iostream>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <csignal>

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

    std::cout << "\n[Tarvis] Entering Passive Listening Mode. (Press Ctrl+C to quit)" << std::endl;

    // Start continuous audio recording
    SDL_PauseAudioDevice(device_id, 0);

    // Main continuous listening loop controlled by 'running' flag
    while (running) {
        // Sleep in 50ms chunks for 2 seconds total, allowing instant exit on Ctrl+C
        int elapsed = 0;
        while (running && elapsed < 2000) {
            SDL_Delay(50);
            elapsed += 50;
        }

        if (!running) {
            break;
        }

        // Safely extract and clear the current audio buffer
        std::vector<float> local_buffer;
        {
            std::lock_guard<std::mutex> lock(audio_mutex);
            if (audio_buffer.size() < 4000) {
                audio_buffer.clear();
                continue;
            }
            local_buffer = audio_buffer;
            audio_buffer.clear();
        }

        // Run Whisper Inference on the captured chunk
        if (whisper_full(ctx, wparams, local_buffer.data(), local_buffer.size()) != 0) {
            std::cerr << "[Tarvis Error] Failed to process audio buffer." << std::endl;
            continue;
        }

        // Extract transcribed text from segments
        std::string transcript = "";
        const int n_segments = whisper_full_n_segments(ctx);
        for (int i = 0; i < n_segments; ++i) {
            transcript += whisper_full_get_segment_text(ctx, i);
        }

        // Trim leading spaces
        while (!transcript.empty() && (transcript.front() == ' ' || transcript.front() == '\t')) {
            transcript.erase(transcript.begin());
        }

        if (transcript.empty() || transcript.find("[BLANK_AUDIO]") != std::string::npos) {
            continue;
        }

        // Output passive text
        std::cout << "[Passive Output]: " << transcript << std::endl;
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
