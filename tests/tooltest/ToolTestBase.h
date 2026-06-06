#pragma once

#include "agent/AgentToolOrchestrator.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <vector>
#include <stdexcept>
#include <iostream>

namespace voice_agent {

/**
 * @brief Testler için Mock Interpreter. SOLID - Interface Segregation.
 */
struct SequenceInterpreter final : IInterpreter {
    std::vector<InterpreterResponse> responses;
    std::vector<InterpreterInput> seenInputs;
    std::size_t nextResponse = 0;

    void ResetSession(std::string) override {}

    InterpreterResponse Interpret(
        const InterpreterInput& input,
        const InterpreterStreamCallback&, 
        const CancellationToken*) override {
        seenInputs.push_back(input);
        if (nextResponse >= responses.size()) {
            return InterpreterResponse{};
        }
        return responses[nextResponse++];
    }
};

/**
 * @brief Tüm Tool testleri için temel sınıf. SOLID - Single Responsibility (Ortak Altyapı).
 */
class ToolTest : public ::testing::Test {
protected:
    std::filesystem::path binaryRoot;
    std::filesystem::path scriptsRoot;
    std::filesystem::path skillsRoot;
    std::filesystem::path experiencesPath;

    ToolTest() {
        binaryRoot = GetBinaryRoot();
        scriptsRoot = binaryRoot / "scripts";
        skillsRoot = binaryRoot / "skills";
        experiencesPath = binaryRoot / "experiences" / "experiences.md";
    }

    void SetUp() override {}

    std::filesystem::path GetBinaryRoot() const {
        std::error_code ec;
        const std::filesystem::path executablePath = std::filesystem::read_symlink("/proc/self/exe", ec);
        if (!ec && !executablePath.empty()) {
            return executablePath.parent_path();
        }
        return std::filesystem::current_path();
    }

    // Ortak Mock Yanıt Oluşturucular
    InterpreterResponse MakeJsonToolCallResponse(const std::string& json) const {
        InterpreterResponse response;
        response.segments.push_back(ResponseSegment{ResponseSegmentType::Json, json, false});
        return response;
    }

    InterpreterResponse MakeSpeechResponse(const std::string& text) const {
        InterpreterResponse response;
        response.segments.push_back(ResponseSegment{ResponseSegmentType::Speech, text, true});
        return response;
    }
};

} // namespace voice_agent
