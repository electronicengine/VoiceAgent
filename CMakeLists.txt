cmake_minimum_required(VERSION 3.20)

project(cpp_voice_agent LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)

find_package(CURL REQUIRED)
find_package(ALSA REQUIRED)

add_executable(cpp_voice_agent
    src/main.cpp
    src/agent/AgentOrchestrator.cpp
    src/audio/AlsaMicrophone.cpp
    src/audio/AlsaSpeaker.cpp
    src/common/HttpClient.cpp
    src/common/StringUtils.cpp
    src/config/AppConfig.cpp
    src/interpreter/OpenAiInterpreter.cpp
    src/synthesizer/AzureRestSynthesizer.cpp
    src/tools/PythonTool.cpp
    src/tools/ShellTool.cpp
    src/transcriber/ITranscriber.cpp
    src/transcriber/AzureTranscriber.cpp
    src/transcriber/DeepgramTranscriber.cpp
    src/voice/VoiceAgent.cpp
)

target_include_directories(cpp_voice_agent PRIVATE src)

target_link_libraries(cpp_voice_agent
    PRIVATE
    ALSA::ALSA
        CURL::libcurl
        nlohmann_json::nlohmann_json
)