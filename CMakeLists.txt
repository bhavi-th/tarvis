cmake_minimum_required(VERSION 3.14)
project(tarvis CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(SDL2 REQUIRED)
find_package(CURL REQUIRED)

add_subdirectory(/home/bhavith/whisper.cpp whisper_build)

add_executable(tarvis
  src/main.cpp
  src/llm_client.cpp
  src/tts_client.cpp
)

target_link_libraries(tarvis PRIVATE whisper SDL2::SDL2 CURL::libcurl)
