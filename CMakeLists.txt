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

# webrtc-audio-processing v2 (provides AEC3). Built from source under
# third_party/webrtc-apm via meson; see README/build instructions.
# Locate via pkg-config; users may also override WEBRTC_APM_PREFIX.
set(WEBRTC_APM_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webrtc-apm"
    CACHE PATH "Install prefix of webrtc-audio-processing v2")

find_package(PkgConfig REQUIRED)
set(_webrtc_apm_pc_dirs
    "${WEBRTC_APM_PREFIX}/lib/pkgconfig"
    "${WEBRTC_APM_PREFIX}/lib/aarch64-linux-gnu/pkgconfig"
    "${WEBRTC_APM_PREFIX}/lib/x86_64-linux-gnu/pkgconfig"
)
foreach(_dir IN LISTS _webrtc_apm_pc_dirs)
    if(EXISTS "${_dir}")
        if(DEFINED ENV{PKG_CONFIG_PATH} AND NOT "$ENV{PKG_CONFIG_PATH}" STREQUAL "")
            set(ENV{PKG_CONFIG_PATH} "${_dir}:$ENV{PKG_CONFIG_PATH}")
        else()
            set(ENV{PKG_CONFIG_PATH} "${_dir}")
        endif()
    endif()
endforeach()

pkg_check_modules(WEBRTC_APM IMPORTED_TARGET webrtc-audio-processing-2)
if(NOT WEBRTC_APM_FOUND)
    message(FATAL_ERROR
        "webrtc-audio-processing-2 (with AEC3) was not found. "
        "Build it from source and install to ${WEBRTC_APM_PREFIX}, e.g.:\n"
        "  git clone https://gitlab.freedesktop.org/pulseaudio/webrtc-audio-processing.git\n"
        "  cd webrtc-audio-processing\n"
        "  meson setup build --prefix=${WEBRTC_APM_PREFIX} --buildtype=release --default-library=static\n"
        "  ninja -C build && ninja -C build install"
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

target_include_directories(cpp_voice_agent PRIVATE src)

target_compile_definitions(cpp_voice_agent PRIVATE WEBRTC_POSIX)

target_link_libraries(cpp_voice_agent
    PRIVATE
        ALSA::ALSA
        CURL::libcurl
        nlohmann_json::nlohmann_json
        PkgConfig::WEBRTC_APM
        fvad
        Threads::Threads
)

# libfvad is built from a sibling fork of WebRTC's signal_processing code and
# exports the same WebRtcSpl_* symbols as webrtc-audio-processing-2. The two
# implementations are functionally equivalent, so allow the linker to keep the
# first occurrence and discard duplicates.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(cpp_voice_agent PRIVATE "LINKER:--allow-multiple-definition")
endif()