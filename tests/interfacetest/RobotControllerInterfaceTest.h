#pragma once
#include <gtest/gtest.h>
#include "interface/RobotControllerInterface.h"
#include "common/llama_operator.h"

namespace voice_agent {

class RobotControllerInterfaceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // We might need a real or dummy model for LlamaOperator depends on how calculateEmbeddings is implemented
    }
};

} // namespace voice_agent
