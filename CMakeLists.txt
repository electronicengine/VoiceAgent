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
# Locate via pkg-config; users may also override WEBRTC_APM_INSTALL_PREFIX.
# (The cache var must NOT start with "WEBRTC_APM_" because pkg_check_modules
# overwrites variables named "<prefix>_PREFIX" with the .pc file's prefix.)
set(WEBRTC_APM_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webrtc-apm"
    CACHE PATH "Install prefix of webrtc-audio-processing v2")
if(NOT WEBRTC_APM_INSTALL_PREFIX)
    set(WEBRTC_APM_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webrtc-apm"
        CACHE PATH "Install prefix of webrtc-audio-processing v2" FORCE)
endif()

find_package(PkgConfig REQUIRED)
set(_webrtc_apm_pc_dirs
    "${WEBRTC_APM_INSTALL_PREFIX}/lib/pkgconfig"
    "${WEBRTC_APM_INSTALL_PREFIX}/lib/aarch64-linux-gnu/pkgconfig"
    "${WEBRTC_APM_INSTALL_PREFIX}/lib/x86_64-linux-gnu/pkgconfig"
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

pkg_check_modules(WEBRTC_APM IMPORTED_TARGET QUIET webrtc-audio-processing-2)

if(NOT WEBRTC_APM_FOUND)
    message(STATUS "webrtc-audio-processing-2 not found; fetching and building from source into ${WEBRTC_APM_INSTALL_PREFIX}")

    find_program(MESON_EXECUTABLE meson)
    find_program(NINJA_EXECUTABLE ninja)
    if(NOT MESON_EXECUTABLE OR NOT NINJA_EXECUTABLE)
        message(FATAL_ERROR
            "Auto-build of webrtc-audio-processing-2 requires 'meson' and 'ninja'.\n"
            "Install them, e.g. on Ubuntu/Debian:\n"
            "  sudo apt install meson ninja-build pkg-config"
        )
    endif()

    FetchContent_Declare(
        webrtc_apm
        GIT_REPOSITORY https://gitlab.freedesktop.org/pulseaudio/webrtc-audio-processing.git
        GIT_TAG        v2.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_GetProperties(webrtc_apm)
    if(NOT webrtc_apm_POPULATED)
        FetchContent_Populate(webrtc_apm)
    endif()

    set(_webrtc_apm_build_dir "${webrtc_apm_BINARY_DIR}/meson-build")
    set(_webrtc_apm_stamp "${WEBRTC_APM_INSTALL_PREFIX}/.voiceagent-installed")

    if(NOT EXISTS "${_webrtc_apm_stamp}")
        file(MAKE_DIRECTORY "${WEBRTC_APM_INSTALL_PREFIX}")

        if(NOT EXISTS "${_webrtc_apm_build_dir}/build.ninja")
            message(STATUS "Configuring webrtc-audio-processing with meson...")
            execute_process(
                COMMAND "${MESON_EXECUTABLE}" setup
                    "${_webrtc_apm_build_dir}"
                    "${webrtc_apm_SOURCE_DIR}"
                    "--prefix=${WEBRTC_APM_INSTALL_PREFIX}"
                    --buildtype=release
                    --default-library=static
                RESULT_VARIABLE _meson_result
            )
            if(NOT _meson_result EQUAL 0)
                message(FATAL_ERROR "meson setup for webrtc-audio-processing failed (exit ${_meson_result}). "
                    "Ensure 'libabsl-dev' (Abseil) and a C++ toolchain are installed.")
            endif()
        endif()

        message(STATUS "Building webrtc-audio-processing with ninja...")
        execute_process(
            COMMAND "${NINJA_EXECUTABLE}" -C "${_webrtc_apm_build_dir}" install
            RESULT_VARIABLE _ninja_result
        )
        if(NOT _ninja_result EQUAL 0)
            message(FATAL_ERROR "ninja install for webrtc-audio-processing failed (exit ${_ninja_result}).")
        endif()

        file(WRITE "${_webrtc_apm_stamp}" "ok\n")
    else()
        message(STATUS "Reusing existing webrtc-audio-processing install at ${WEBRTC_APM_INSTALL_PREFIX}")
    endif()

    # Re-prime PKG_CONFIG_PATH (the freshly-installed .pc may live under a multiarch dir)
    foreach(_dir IN LISTS _webrtc_apm_pc_dirs)
        if(EXISTS "${_dir}")
            if(DEFINED ENV{PKG_CONFIG_PATH} AND NOT "$ENV{PKG_CONFIG_PATH}" STREQUAL "")
                set(ENV{PKG_CONFIG_PATH} "${_dir}:$ENV{PKG_CONFIG_PATH}")
            else()
                set(ENV{PKG_CONFIG_PATH} "${_dir}")
            endif()
        endif()
    endforeach()

    # Reset cached pkg-config results so the next call re-queries.
    unset(WEBRTC_APM_FOUND CACHE)
    unset(WEBRTC_APM_LIBRARIES CACHE)
    unset(WEBRTC_APM_INCLUDE_DIRS CACHE)
    pkg_check_modules(WEBRTC_APM IMPORTED_TARGET REQUIRED webrtc-audio-processing-2)
endif()

# webrtc-audio-processing-2 v2.1's public headers include "absl/..." paths, but
# the bundled Abseil headers are installed alongside under <prefix>/include
# (the .pc file only adds <prefix>/include/webrtc-audio-processing-2). Add the
# extra include dir so absl/* resolves.
if(EXISTS "${WEBRTC_APM_INSTALL_PREFIX}/include/absl")
    target_include_directories(PkgConfig::WEBRTC_APM INTERFACE
        "${WEBRTC_APM_INSTALL_PREFIX}/include")
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
    src/common/StdinUserPromptProvider.cpp
    src/common/StringUtils.cpp
    src/common/VoiceUserPromptProvider.cpp
    src/config/AccountStore.cpp
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