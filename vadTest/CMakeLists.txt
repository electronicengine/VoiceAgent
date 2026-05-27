cmake_minimum_required(VERSION 3.10)
project(robot_audio CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WEBRTC REQUIRED webrtc-audio-processing)
pkg_check_modules(FVAD   REQUIRED fvad)

add_executable(robot_audio
    main.cpp
    microphone.cpp
    speaker.cpp
    speech_processor.cpp
    robot_audio_controller.cpp
)

target_include_directories(robot_audio PRIVATE
    ${WEBRTC_INCLUDE_DIRS}
    ${FVAD_INCLUDE_DIRS}
)

target_link_libraries(robot_audio
    ${WEBRTC_LIBRARIES}
    ${FVAD_LIBRARIES}
    asound       # ALSA
    pthread      # std::thread
)

target_compile_options(robot_audio PRIVATE
    ${WEBRTC_CFLAGS_OTHER}
    ${FVAD_CFLAGS_OTHER}
)
