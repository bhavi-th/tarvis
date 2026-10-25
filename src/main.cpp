#include "llm_client.h"
#include "text_formatter.h"
#include "tts_client.h"
#include "whisper.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <csignal>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

std::vector<float> audio_buffer;
std::mutex audio_mutex;
std::atomic<bool> running(true);
std::atomic<long long> total_samples_captured(0);

void signal_handler(int signal) {
  if (signal == SIGINT) {
    std::cout << "\n[Tarvis] Interrupt received. Shutting down gracefully..."
              << std::endl;
    running = false;
  }
}

void audio_callback(void *userdata, Uint8 *stream, int len) {
  int n_samples = len / sizeof(int16_t);
  int16_t *samples = (int16_t *)stream;

  std::lock_guard<std::mutex> lock(audio_mutex);
  for (int i = 0; i < n_samples; ++i) {
    float sample = (float)samples[i] / 32768.0f;
    audio_buffer.push_back(sample);
    total_samples_captured++;
  }
}

bool contains_keyword(const std::string &str, const std::string &keyword) {
  std::string str_lower = str;
  std::string kw_lower = keyword;
  std::transform(str_lower.begin(), str_lower.end(), str_lower.begin(),
                 ::tolower);
  std::transform(kw_lower.begin(), kw_lower.end(), kw_lower.begin(), ::tolower);
  return str_lower.find(kw_lower) != std::string::npos;
}

int main() {
  std::signal(SIGINT, signal_handler);

  std::cout << "[Tarvis] Initializing local AI voice engine..." << std::endl;

  struct whisper_context_params cparams = whisper_context_default_params();
  struct whisper_context *ctx = whisper_init_from_file_with_params(
      "/home/bhavith/whisper.cpp/ggml-base.en.bin", cparams);
  if (!ctx) {
    std::cerr << "[Tarvis Error] Failed to load Whisper model!" << std::endl;
    return 1;
  }
  std::cout << "[Tarvis Success] Whisper model loaded into memory!"
            << std::endl;

  if (SDL_Init(SDL_INIT_AUDIO) < 0) {
    std::cerr << "[Tarvis Error] SDL could not initialize! SDL_Error: "
              << SDL_GetError() << std::endl;
    whisper_free(ctx);
    return 1;
  }

  SDL_AudioSpec desired_spec;
  SDL_zero(desired_spec);
  desired_spec.freq = 16000;
  desired_spec.format = AUDIO_S16SYS;
  desired_spec.channels = 1;
  desired_spec.samples = 1024;
  desired_spec.callback = audio_callback;

  SDL_AudioSpec obtained_spec;
  SDL_AudioDeviceID device_id =
      SDL_OpenAudioDevice(nullptr, 1, &desired_spec, &obtained_spec, 0);
  if (device_id == 0) {
    std::cerr << "[Tarvis Error] Failed to open audio device: "
              << SDL_GetError() << std::endl;
    SDL_Quit();
    whisper_free(ctx);
    return 1;
  }

  struct whisper_full_params wparams =
      whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
  wparams.print_progress = false;
  wparams.language = "en";
  wparams.n_threads = 4;
  wparams.initial_prompt = "Tarvis";

  std::cout << "\n[Tarvis] Entering Active VAD Listening Mode. (Speak your "
               "paragraph, pause to convert)"
            << std::endl;

  SDL_PauseAudioDevice(device_id, 0);

  const float VOICE_THRESHOLD = 0.015f;
  const int SILENCE_DURATION_MS = 800;
  const int CHECK_INTERVAL_MS = 100;

  std::vector<float> speech_buffer;
  bool is_speaking = false;
  int silence_counter = 0;

  bool is_awake = false;

  while (running) {
    SDL_Delay(CHECK_INTERVAL_MS);

    if (!running) {
      break;
    }

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

    float sum_squares = 0.0f;
    for (float sample : chunk) {
      sum_squares += sample * sample;
    }
    float rms = std::sqrt(sum_squares / chunk.size());

    speech_buffer.insert(speech_buffer.end(), chunk.begin(), chunk.end());

    if (rms > VOICE_THRESHOLD) {
      if (!is_speaking) {
        std::cout << "[Tarvis] Speech detected... listening..." << std::endl;
        is_speaking = true;
        speech_buffer.clear();
      }
      silence_counter = 0;
    } else if (is_speaking) {
      silence_counter += CHECK_INTERVAL_MS;

      if (silence_counter >= SILENCE_DURATION_MS) {
        std::cout << "[Tarvis] Pause detected. Processing full paragraph..."
                  << std::endl;

        if (speech_buffer.size() > 8000) {
          if (whisper_full(ctx, wparams, speech_buffer.data(),
                           speech_buffer.size()) == 0) {
            std::string transcript = "";
            const int n_segments = whisper_full_n_segments(ctx);
            for (int i = 0; i < n_segments; ++i) {
              transcript += whisper_full_get_segment_text(ctx, i);
            }

            while (!transcript.empty() &&
                   (transcript.front() == ' ' || transcript.front() == '\t')) {
              transcript.erase(transcript.begin());
            }

            if (!transcript.empty() &&
                transcript.find("[BLANK_AUDIO]") == std::string::npos) {
              std::cout << "\n[Transcript]: " << transcript << "\n\n";

              if (!is_awake) {
                if (contains_keyword(transcript, "tarvis")) {
                  is_awake = true;
                  std::cout << "[Tarvis] Wake word recognized! Starting up...."
                            << std::endl;
                  speak_text("I'm awake.");
                } else {
                  std::cout << "[Tarvis] Sleeping. Ignoring input."
                            << std::endl;
                }
              } else {

                std::cout << "[TARVIS AI]: " << std::flush;

                std::string llm_response =
                    query_lm_studio(transcript, [](const std::string &chunk) {
                      std::cout << chunk << std::flush;
                    });

                std::cout << std::endl;

                std::string tts_ready_text = clean_text_for_tts(llm_response);
                speak_text(tts_ready_text);

                if (contains_keyword(transcript, "sleep")) {
                  is_awake = false;
                  std::cout << "[Tavis] Sleep command detected. Entering sleep "
                               "mode..."
                            << std::endl;
                }

                if (contains_keyword(transcript, "bye") ||
                    contains_keyword(transcript, "goodbye")) {
                  std::cout << "[Tarvis] Farewell detected. Shutting down..."
                            << std::endl;
                  running = false;
                }
              }
            }
          } else {
            std::cerr << "[Tarvis Error] Failed to process audio buffer."
                      << std::endl;
          }

          whisper_reset_timings(ctx);
        }

        speech_buffer.clear();
        is_speaking = false;
        silence_counter = 0;
        std::cout << "[Tarvis] Resuming listening..." << std::endl;
      }
    } else {
      if (speech_buffer.size() > 16000 * 2) {
        speech_buffer.erase(speech_buffer.begin(), speech_buffer.end() - 8000);
      }
    }
  }

  std::cout << "[Tarvis] Releasing hardware and memory resources..."
            << std::endl;
  SDL_PauseAudioDevice(device_id, 1);
  SDL_CloseAudioDevice(device_id);
  SDL_Quit();
  whisper_free(ctx);
  std::cout << "[Tarvis] Exited successfully." << std::endl;

  return 0;
}
