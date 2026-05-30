cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

project(cpp_voice_agent LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
    libfvad
    GIT_REPOSITORY https://github.com/dpirch/libfvad.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(libfvad)

find_package(CURL QUIET)
if(NOT CURL_FOUND)
    message(FATAL_ERROR
        "libcurl runtime is installed, but the development package is missing. "
        "Install libcurl4-openssl-dev (Ubuntu/Debian) so CMake can find curl/curl.h and libcurl."
    )
endif()

find_package(ALSA REQUIRED)
find_package(Threads REQUIRED)

find_path(WEBRTC_AUDIO_PROCESSING_INCLUDE_DIR
    NAMES webrtc/modules/audio_processing/include/audio_processing.h
    PATHS
        /usr/include/webrtc_audio_processing
        /usr/local/include/webrtc_audio_processing
        /usr/include
        /usr/local/include
)
find_library(WEBRTC_AUDIO_PROCESSING_LIBRARY
    NAMES webrtc_audio_processing
    PATHS
        /usr/lib
        /usr/local/lib
        /usr/lib/x86_64-linux-gnu
        /usr/lib/aarch64-linux-gnu
)

if(NOT WEBRTC_AUDIO_PROCESSING_INCLUDE_DIR OR NOT WEBRTC_AUDIO_PROCESSING_LIBRARY)
    message(FATAL_ERROR
        "WebRTC AudioProcessing development files are required for the voice activity detector. "
        "Install the headers and libwebrtc_audio_processing before configuring this project."
    )
endif()

add_executable(cpp_voice_agent
    src/main.cpp
    src/agent/Agent.cpp
    src/agent/AgentToolOrchestrator.cpp
    src/agent/TextAgent.cpp
    src/agent/VoiceAgent.cpp
    src/audio/AlsaMicrophone.cpp
    src/audio/AlsaSpeaker.cpp
    src/audio/EchoCanceller.cpp
    src/audio/VadDetector.cpp
    src/audio/VoiceActivityDetector.cpp
    src/audio/VoiceController.cpp
    src/common/HttpClient.cpp
    src/common/StringUtils.cpp
    src/config/AppConfig.cpp
    src/interpreter/OpenAiInterpreter.cpp
    src/synthesizer/AzureRestSynthesizer.cpp
    src/tools/PythonTool.cpp
    src/tools/ShellTool.cpp
    src/tools/WebBrowserTool.cpp
    src/transcriber/ITranscriber.cpp
    src/transcriber/AzureTranscriber.cpp
    src/transcriber/DeepgramTranscriber.cpp
)

target_include_directories(cpp_voice_agent PRIVATE src ${WEBRTC_AUDIO_PROCESSING_INCLUDE_DIR})

target_compile_definitions(cpp_voice_agent PRIVATE WEBRTC_AUDIO_PROCESSING_ONLY_BUILD WEBRTC_POSIX)

target_link_libraries(cpp_voice_agent
    PRIVATE
        ALSA::ALSA
        CURL::libcurl
        nlohmann_json::nlohmann_json
        ${WEBRTC_AUDIO_PROCESSING_LIBRARY}
        fvad
        Threads::Threads
)