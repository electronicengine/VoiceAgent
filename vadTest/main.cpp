#include <exception>
#include <iostream>
#include "robot_audio_controller.h"

int main() {
    try {
        robot_audio::RobotAudioController controller;
        controller.run();
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }

    return 0;
}
